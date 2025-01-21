'use strict';

var spectrum, logger, ws;

function connectWebSocket(spectrum) {

    //ws = new WebSocket("ws://" + window.location.host + ":7681/waterfall");
    //ws = new WebSocket("ws://" + window.location.hostname + ":8000/waterfall");
    ws = new WebSocket("wss://" + window.location.hostname + "/waterfall");
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

function showMIDIMessages(message) {
    const midiMessagesDiv = document.getElementById('midiMessages');
    const data = message.data;
    const midiMessage = `Message MIDI reçu : [${data[0]}, ${data[1]}, ${data[2]}]`;
    const messageElement = document.createElement('p');
    messageElement.textContent = midiMessage;
    midiMessagesDiv.appendChild(messageElement);
}

// Fonction pour gérer les événements MIDI
function handleMIDIMessage(event) {
    showMIDIMessages(event);
    console.log('Message MIDI reçu :', event.data);
}

// Fonction pour accéder aux périphériques MIDI
function accessMIDIDevices(midiAccess) {
    const midiStatus = document.getElementById('midiStatus');
    

    // Écouter les événements MIDI pour chaque entrée MIDI
    const inputs = midiAccess.inputs.values();
    for (let input = inputs.next(); input && !input.done; input = inputs.next()) {
        input.value.onmidimessage = handleMIDIMessage;
    }
}

// Fonction pour gérer les erreurs lors de l'accès aux périphériques MIDI
function onMIDIFailure() {
    const midiStatus = document.getElementById('midiStatus');
   
    console.error('Échec de l\'accès aux périphériques MIDI.');
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
    window.addEventListener('wheel',function (e)  
    {
        spectrum.handleMouseWheel(e);
    });    
    window.addEventListener('mousedown',function (e)  
    {
        spectrum.handleMouseDown(e);
    });   
    window.addEventListener('mouseup',function (e)  
    {
        spectrum.handleMouseUp(e);
    });   
    window.addEventListener('mousemove',function (e)  
    {
        spectrum.handleMouseMove(e);
    });  

   // FIXME : NEED HTTPS !!!!!
    if (navigator.requestMIDIAccess) {
        console.log('This browser supports WebMIDI!');
    } else {
        console.log('WebMIDI is not supported in this browser.');
    }

}

window.onload = main;
