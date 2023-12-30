module.exports = {
    serial: {
        portId: '/dev/ttyUSB0',
        baudRate: 1200,
        dataBits: 7,
        stopBits: 1,
        parity: 'even'
    },
    mqtt: {
        host: '192.168.1.21',
        port: '1883'
    }
}
