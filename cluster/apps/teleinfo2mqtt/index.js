const raspi = require('raspi')
const Serial = require('raspi-serial').Serial
const config = require('./config')
const mqtt = require('mqtt')

raspi.init(() => {

	const client = mqtt.connect(`mqtt://${config.mqtt.host}:${config.mqtt.port}`, { clientId: `teleinfo_${Math.random().toString(16).slice(3)}` })

	client.on('connect', () => {

		console.log('MQTT connected')

		const serial = new Serial(config.serial)

		serial.open(() => {

			let topic, buffer = '', match, object = {}

			let serialNumber, tariffOption, subscribedIntensity,
				indexHC, indexHP,
				currentPeriodTariff,
				instantaneousIntensity,
				maximumIntensity,
				maximumPower, apparentPower, timeGroup, check

			serial.on('data', data => {

				data = data.toString()
				buffer += data

				// Start of the buffer
				if (data.startsWith('\u0002\nADCO')) {
					buffer = buffer.replace('\u0002\n', '')
				}

				// End of the buffer
				if (data.startsWith('\u0003')) {
					
					buffer = buffer.replace('\u0003', '')

					// console.debug('Parsing buffer')

					match = /ADCO ([0-9]+)/g.exec(buffer)

					if (match && match[1] && match[1]) {

						serialNumber = match[1]
						topic = `teleinfo/linky_${serialNumber.slice(-6)}`
						object.serialNumber = serialNumber

						match = /OPTARIF ([A-Z]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== tariffOption) {
							tariffOption = match[1]
							object.tariffOption = tariffOption
						}

						match = /ISOUSC ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== subscribedIntensity) {
							subscribedIntensity = match[1]
							object.subscribedIntensity = { value: subscribedIntensity, unit: 'A' }
						}

						match = /HCHC ([0-9]+) /g.exec(buffer)
						if (match && match[1] && match[1] !== indexHC) {
							indexHC = match[1]
							object.indexHC = { value: indexHC, unit: 'Wh' }
						}

						match = /HCHP ([0-9]+) /g.exec(buffer)
						if (match && match[1] && match[1] !== indexHP) {
							indexHP = match[1]
							object.indexHP = { value: indexHP, unit: 'Wh' }
						}

						match = /PTEC ([A-Z]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== currentPeriodTariff) {
							currentPeriodTariff = match[1]
							object.currentPeriodTariff = currentPeriodTariff
						}

						match = /IINST ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== instantaneousIntensity) {
							instantaneousIntensity = match[1]
							object.instantaneousIntensity = { value: instantaneousIntensity, unit: 'A' }
						}

						match = /IMAX ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== maximumIntensity) {
							maximumIntensity = match[1]
							object.maximumIntensity = { value: maximumIntensity, unit: 'A' }
						}

						match = /PMAX ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== maximumPower) {
							maximumPower = match[1]
							object.maximumPower = { value: maximumPower, unit: 'W' }
						}

						match = /PAPP ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== apparentPower) {
							apparentPower = match[1]
							object.apparentPower = { value: apparentPower, unit: 'VA' }
						}

						match = /HHPHC ([A-Z]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== timeGroup) {
							timeGroup = match[1]
							object.timeGroup = timeGroup
						}

						match = /MOTDETAT ([0-9]+)/g.exec(buffer)
						if (match && match[1] && match[1] !== check) {
							check = match[1]
							object.check = check
						}

						// console.debug(`Publish ${topic}`, object)
						client.publish(`${topic}`, JSON.stringify(object))

						// Reset buffer
						buffer = ''
					}
				}
			})
		})
	})

	client.on('error', err => { console.error(err) })
})
