const fs = require('fs')
const { execFileSync } = require('child_process')
const mqtt = require('mqtt')
const config = require('./config')

const STX = ''
const ETX = ''
const MAX_FRAME_LENGTH = 4096
const NO_DATA_TIMEOUT_MS = 60000

// Historic teleinfo label -> published field
const FIELDS = {
	OPTARIF: { key: 'tariffOption', pattern: /^[A-Z]+/ },
	ISOUSC: { key: 'subscribedIntensity', pattern: /^[0-9]+/, unit: 'A' },
	HCHC: { key: 'indexHC', pattern: /^[0-9]+/, unit: 'Wh' },
	HCHP: { key: 'indexHP', pattern: /^[0-9]+/, unit: 'Wh' },
	PTEC: { key: 'currentPeriodTariff', pattern: /^[A-Z]+/ },
	IINST: { key: 'instantaneousIntensity', pattern: /^[0-9]+/, unit: 'A' },
	IMAX: { key: 'maximumIntensity', pattern: /^[0-9]+/, unit: 'A' },
	PMAX: { key: 'maximumPower', pattern: /^[0-9]+/, unit: 'W' },
	PAPP: { key: 'apparentPower', pattern: /^[0-9]+/, unit: 'VA' },
	HHPHC: { key: 'timeGroup', pattern: /^[A-Z]+/ },
	MOTDETAT: { key: 'check', pattern: /^[0-9]+/ }
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

// Line: label SP data SP checksum. Some meters include the last SP in the checksum.
const parseLine = line => {
	if (line.length < 5) return null
	const body = line.slice(0, -1)
	const payload = body.slice(0, -1)
	const received = line.slice(-1)
	if (checksum(payload) !== received && checksum(body) !== received) return null
	const separator = payload.search(/[ \t]/)
	if (separator < 1) return null
	return { label: payload.slice(0, separator), value: payload.slice(separator + 1) }
}

const parseFrame = (frame, state) => {
	let serialNumber
	for (const line of frame.split(/[\r\n]+/)) {
		const parsed = parseLine(line)
		if (!parsed) continue
		if (parsed.label === 'ADCO') {
			const match = /^[0-9]+/.exec(parsed.value)
			if (match) serialNumber = match[0]
			continue
		}
		const field = FIELDS[parsed.label]
		if (!field) continue
		const match = field.pattern.exec(parsed.value)
		if (!match) continue
		state[field.key] = field.unit ? { value: match[0], unit: field.unit } : match[0]
	}
	return serialNumber
}

const openSerial = ({ portId, baudRate, dataBits, stopBits, parity }) => {
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

let serial
try {
	serial = openSerial(config.serial)
} catch (err) {
	fatal('Cannot open serial port', err)
}

const state = {}
let buffer = null
let watchdog

const resetWatchdog = () => {
	clearTimeout(watchdog)
	watchdog = setTimeout(() => fatal(`No teleinfo frame for ${NO_DATA_TIMEOUT_MS / 1000}s`), NO_DATA_TIMEOUT_MS)
}
resetWatchdog()

serial.on('data', chunk => {
	for (const char of chunk) {
		if (char === STX) {
			buffer = ''
		} else if (char === ETX) {
			if (buffer !== null) {
				const serialNumber = parseFrame(buffer, state)
				if (serialNumber) {
					resetWatchdog()
					state.serialNumber = serialNumber
					if (client.connected) {
						client.publish(`teleinfo/linky_${serialNumber.slice(-6)}`, JSON.stringify(state))
					}
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

process.on('SIGTERM', () => process.exit(0))
