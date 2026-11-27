module.exports = {
    serial: {
        portId: '/dev/ttyUSB0',
        // First TIC mode tried at startup; the other one is tried when no valid frame arrives.
        mode: 'standard',
        dataBits: 7,
        stopBits: 1,
        parity: 'even'
    },
    mqtt: {
        host: '192.168.1.21',
        port: '1883'
    }
}
