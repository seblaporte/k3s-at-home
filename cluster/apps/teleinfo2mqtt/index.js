const fs = require('fs')
const { execFileSync } = require('child_process')
const mqtt = require('mqtt')
const config = require('./config')

const STX = '\x02'
const ETX = '\x03'
const MAX_FRAME_LENGTH = 4096
// Without a valid frame for this long, the other TIC mode is tried; after both, the process exits.
const NO_DATA_TIMEOUT_MS = 30000
// A field missing from the frames for this long is no longer published (stale value).
const STALE_FIELD_MS = 30000

const number = (key, unit) => ({ key, pattern: /^[0-9]+/, unit })
// Text values are padded with spaces by the meter.
const text = key => ({ key, pattern: /\S(.*\S)?/ })
const numbered = (count, label, key) => Object.fromEntries(Array.from({ length: count }, (_, i) => {
	const n = String(i + 1).padStart(2, '0')
	return [`${label}${n}`, number(`${key}${n}`, 'Wh')]
}))

// TIC label -> published field. apparentPower and instantaneousIntensity keep the same key in both modes.
const MODES = {
	historic: {
		baudRate: 1200,
		serialLabel: 'ADCO',
		fields: {
			OPTARIF: text('tariffOption'),
			ISOUSC: number('subscribedIntensity', 'A'),
			BASE: number('indexBase', 'Wh'),
			HCHC: number('indexHC', 'Wh'),
			HCHP: number('indexHP', 'Wh'),
			PTEC: text('currentPeriodTariff'),
			IINST: number('instantaneousIntensity', 'A'),
			IMAX: number('maximumIntensity', 'A'),
			PMAX: number('maximumPower', 'W'),
			PAPP: number('apparentPower', 'VA'),
			HHPHC: text('timeGroup'),
			MOTDETAT: text('check')
		}
	},
	standard: {
		baudRate: 9600,
		serialLabel: 'ADSC',
		fields: {
			NGTF: text('tariffName'),
			LTARF: text('currentTariffLabel'),
			NTARF: number('currentTariffIndex'),
			EAST: number('totalIndex', 'Wh'),
			...numbered(10, 'EASF', 'supplierIndex'),
			...numbered(4, 'EASD', 'distributorIndex'),
			IRMS1: number('instantaneousIntensity', 'A'),
			URMS1: number('rmsVoltage', 'V'),
			PREF: number('subscribedPower', 'kVA'),
			PCOUP: number('breakingPower', 'kVA'),
			SINSTS: number('apparentPower', 'VA'),
			SMAXSN: number('maximumPowerToday', 'VA'),
			STGE: text('status')
		}
	}
}

const fatal = (message, err) => {
	console.error(message, err || '')
	process.exit(1)
}

const checksum = text => {
	let sum = 0
	for (let i = 0; i < text.length; i++) sum += text.charCodeAt(i)
	return String.fromCharCode((sum & 0x3f) + 0x20)
}

// Historic line: label SP data SP checksum. Some meters include the last SP in the checksum.
// Standard line: label HT [timestamp HT] data HT checksum, the last HT included in the checksum.
// Standard values may contain spaces, so a line holding a HT is split on HT only.
const parseLine = line => {
	if (line.length < 5) return null
	const body = line.slice(0, -1)
	const payload = body.slice(0, -1)
	const received = line.slice(-1)
	if (checksum(payload) !== received && checksum(body) !== received) return null
	if (payload.includes('\t')) {
		const parts = payload.split('\t')
		if (!parts[0]) return null
		return { label: parts[0], value: parts[parts.length - 1] }
	}
	const separator = payload.indexOf(' ')
	if (separator < 1) return null
	return { label: payload.slice(0, separator), value: payload.slice(separator + 1) }
}

const parseFrame = (frame, mode) => {
	let serialNumber
	const values = {}
	for (const line of frame.split(/[\r\n]+/)) {
		const parsed = parseLine(line)
		if (!parsed) continue
		if (parsed.label === mode.serialLabel) {
			const match = /^[0-9]+/.exec(parsed.value)
			if (match) serialNumber = match[0]
			continue
		}
		const field = mode.fields[parsed.label]
		if (!field) continue
		const match = field.pattern.exec(parsed.value)
		if (!match) continue
		values[field.key] = field.unit ? { value: match[0], unit: field.unit } : match[0]
	}
	return { serialNumber, values }
}

const openSerial = ({ portId, dataBits, stopBits, parity }, baudRate) => {
	// Blocking reads (min 1) in the libuv threadpool: no polling loop.
	const args = ['-F', portId, 'raw', String(baudRate), `cs${dataBits}`, 'clocal', 'cread', '-crtscts', '-hupcl', 'min', '1', 'time', '0']
	if (parity === 'even') args.push('parenb', '-parodd')
	else if (parity === 'odd') args.push('parenb', 'parodd')
	else args.push('-parenb')
	args.push(stopBits === 2 ? 'cstopb' : '-cstopb')
	try {
		execFileSync('stty', args, { stdio: ['ignore', 'ignore', 'pipe'] })
	} catch (err) {
		console.warn(`stty reported an error, continuing (checksum and watchdog guard against a bad line setup): ${err.message}`)
	}

	const fd = fs.openSync(portId, fs.constants.O_RDONLY | fs.constants.O_NOCTTY)
	return fs.createReadStream(null, { fd, encoding: 'latin1' })
}

const client = mqtt.connect(`mqtt://${config.mqtt.host}:${config.mqtt.port}`, {
	clientId: `teleinfo_${Math.random().toString(16).slice(3)}`
})
client.on('connect', () => console.log('MQTT connected'))
client.on('error', err => console.error(err))

const state = {}
const seenAt = {}
let modeName = MODES[config.serial.mode] ? config.serial.mode : 'standard'
let failedModes = 0
let serial
let watchdog

const publish = (serialNumber, values) => {
	const now = Date.now()
	for (const [key, value] of Object.entries(values)) {
		state[key] = value
		seenAt[key] = now
	}
	for (const key of Object.keys(seenAt)) {
		if (now - seenAt[key] > STALE_FIELD_MS) {
			delete state[key]
			delete seenAt[key]
		}
	}
	if (client.connected) {
		client.publish(`teleinfo/linky_${serialNumber.slice(-6)}`, JSON.stringify({ ...state, ticMode: modeName, serialNumber }))
	}
}

const resetWatchdog = () => {
	clearTimeout(watchdog)
	watchdog = setTimeout(() => {
		failedModes++
		if (failedModes >= Object.keys(MODES).length) fatal(`No teleinfo frame for ${NO_DATA_TIMEOUT_MS / 1000}s in any TIC mode`)
		const previous = modeName
		modeName = previous === 'standard' ? 'historic' : 'standard'
		console.warn(`No teleinfo frame for ${NO_DATA_TIMEOUT_MS / 1000}s in ${previous} mode, trying ${modeName} mode`)
		start()
	}, NO_DATA_TIMEOUT_MS)
}

const start = () => {
	if (serial) {
		// The old stream must not trigger the fatal close handlers.
		serial.removeAllListeners()
		serial.on('error', () => {})
		serial.destroy()
	}
	for (const key of Object.keys(state)) delete state[key]
	for (const key of Object.keys(seenAt)) delete seenAt[key]

	const mode = MODES[modeName]
	try {
		serial = openSerial(config.serial, mode.baudRate)
	} catch (err) {
		fatal('Cannot open serial port', err)
	}
	console.log(`Reading teleinfo in ${modeName} mode (${mode.baudRate} bauds)`)

	let buffer = null
	serial.on('data', chunk => {
		for (const char of chunk) {
			if (char === STX) {
				buffer = ''
			} else if (char === ETX) {
				if (buffer !== null) {
					const { serialNumber, values } = parseFrame(buffer, mode)
					if (serialNumber) {
						failedModes = 0
						resetWatchdog()
						publish(serialNumber, values)
					}
				}
				buffer = null
			} else if (buffer !== null) {
				buffer += char
				if (buffer.length > MAX_FRAME_LENGTH) buffer = null
			}
		}
	})
	serial.on('error', err => fatal('Serial error', err))
	serial.on('end', () => fatal('Serial port closed'))
	serial.on('close', () => fatal('Serial port closed'))
	resetWatchdog()
}

start()

process.on('SIGTERM', () => process.exit(0))
