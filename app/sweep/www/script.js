'use strict';

var spectrum, logger, ws;

function connectWebSocket(spectrum) {

    //ws = new WebSocket("ws://" + window.location.host + ":7681/waterfall");
    ws = new WebSocket("ws://" + window.location.host + ":8000/waterfall");
    //ws = new WebSocket("ws://10.0.0.105:7681/waterfall");
    spectrum.setWebSocket(ws);

    ws.onconnect = function (evt) {
        ws.binaryType = 'arraybuffer';
    }

    ws.onopen = function (evt) {
        ws.binaryType = 'arraybuffer';
        console.log("connected!");
    }
    ws.onclose = function (evt) {
        console.log("closed");
        setTimeout(function () {
            connectWebSocket(spectrum);
        }, 1000);
    }
    ws.onerror = function (evt) {
        console.log("error: " + evt.message);
    }
    ws.onmessage = function (evt) {

        //spectrum.addData(evt.data);

        if (evt.data instanceof ArrayBuffer) {

            spectrum.addData(evt.data);
        }
        else {
            var data = JSON.parse(evt.data);

            if (data.center) {
                spectrum.setCenterHz(data.center);
            }
            if (data.span) {
                spectrum.setSpanHz(data.span);
            }
            if (data.gain) {
                spectrum.setGain(data.gain);
            }
            if (data.framerate) {
                spectrum.setFps(data.framerate);
            }
            spectrum.log(" > Freq:" + data.center / 1000000 + " MHz | Span: " + data.span / 1000000 + " MHz | Gain: " + data.gain + "dB | Fps: " + data.framerate);

        }
    }
}


function main() {

    // Create spectrum object on canvas with ID "waterfall"
    spectrum = new Spectrum(
        "waterfall", {
        spectrumPercent: 50,
        logger: 'log',
    });

    // Connect to websocket
    connectWebSocket(spectrum);

    // Bind keypress handler
    window.addEventListener("keydown", function (e) {
        spectrum.onKeypress(e);
    });


}

window.onload = main;
