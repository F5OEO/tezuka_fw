'use strict';

var spectrum, logger, ws, mqtt_client;

function connectWebSocket(spectrum) {

    //ws = new WebSocket("ws://" + window.location.host + ":7681/waterfall");
    ws = new WebSocket("ws://" + window.location.hostname + ":8000/waterfall");
    //ws = new WebSocket("wss://" + window.location.hostname + "/waterfall");
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

// called when the client connects
function onConnect() {
    // Once a connection has been made, make a subscription and send a message.
    console.log("MQTT onConnect");
    mqtt_client.subscribe("state/#");
    var message = new Paho.MQTT.Message("0");
    message.destinationName = "cmd/rx/sweep/activate";
    mqtt_client.send(message);
    
    
  }
  
  // called when the client loses its connection
  function onConnectionLost(responseObject) {
    if (responseObject.errorCode !== 0) {
      console.log("onConnectionLost:"+responseObject.errorMessage);
    }
  }
  
  // called when a message arrives
  function onMessageArrived(message) {
    //console.log("onMessageArrived:"+message.destinationName+" "+message.payloadString);
    switch(message.destinationName)
    {
        case "state/rx/frequency" : spectrum.setCenterHz(message.payloadString);spectrum.orginalfreq=message.payloadString;break;
        case "state/rx/sampling" : 
            spectrum.NativeSpan=message.payloadString;
            if(spectrum.onsweep==0)
            {
                //spectrum.setSpanHz(message.payloadString);
            }    
            break;

        case "state/rx/sweep/activate" :
             if(message.payloadString == "1")
                 spectrum.onsweep=1; 
             else 
                spectrum.onsweep=0;
             break;
        case "state/rx/sweep/frequency" :
                if(spectrum.onsweep == 1)
                {
                    //spectrum.setCenterHz(message.payloadString);spectrum.orginalfreq=message.payloadString;
                }
                
                   
                break;
        case "state/rx/sweep/span" :
                    if(spectrum.onsweep == 1)
                    {
                        //spectrum.setSpanHz(message.payloadString);
                    }
                    
                       
                    break;       
        case "state/rx/overload" :
            if(message.payloadString == "1") 
            {
                document.getElementById("rfgain_val").style.backgroundColor = 'red';        
            }
            else
            {
                document.getElementById("rfgain_val").style.backgroundColor = 'green';        
            }
            break;
    }
  }

  const gain = document.getElementById("rfgain");
  function updateGain()
  {
    document.getElementById("rfgain_val").innerText = gain.value;
    var message = new Paho.MQTT.Message(gain.value);
    message.destinationName = "cmd/rx/gain";
    mqtt_client.send(message);
    //ad9361_gain(Number(gain.value));

  }

  const rfinput = document.getElementById("rfinput");
  function updateRxinput()
  {
    console.log("RFinput "+rfinput.value);
    var message = new Paho.MQTT.Message(rfinput.value);
    message.destinationName = "cmd/rx/rfinput";
    mqtt_client.send(message);
  }

function main() {

    mqtt_client = new Paho.MQTT.Client(window.location.hostname, Number(9001), "clientId");
    // set callback handlers
    mqtt_client.onConnectionLost = onConnectionLost;
    mqtt_client.onMessageArrived = onMessageArrived;

    // Create spectrum object on canvas with ID "waterfall"
    spectrum = new Spectrum(
        "waterfall", {
        spectrumPercent: 50,
        logger: 'log'
        
    });

    // Connect to websocket
    connectWebSocket(spectrum);

    // connect the client
    mqtt_client.connect({onSuccess:onConnect});

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
    window.addEventListener('resize',function (e)  
    {
        spectrum.resize();
        
    });  

   
    gain.addEventListener("input", updateGain);
    rfinput.addEventListener("input", updateRxinput);
    updateGain() ;
    
   // FIXME : NEED HTTPS !!!!!
    if (navigator.requestMIDIAccess) {
        console.log('This browser supports WebMIDI!');
    } else {
        console.log('WebMIDI is not supported in this browser.');
    }

}

window.onload = main;
