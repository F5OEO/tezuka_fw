// URL de l'API REST
//const apiUrl = "/api";
const apiUrl = "http://"+window.location.hostname + ":8000"

// Fonction pour appeler l'API REST
async function fetchData() {
    try {
        // Appel à l'API REST en utilisant fetch
        const response = await fetch(apiUrl+"/api", {
            method: 'GET', // Méthode HTTP (GET, POST, etc.)
            headers: {
                'Content-Type': 'application/json' // Type de contenu attendu
            
            },
            credentials: "include" 
       
    });   
        // Vérification de la réponse
        if (!response.ok) {
            throw new Error('Erreur dans la requête API : ' + response.statusText);
        }

        // Conversion de la réponse en JSON
        const data = await response.json();

        // Affichage des données dans la console
        console.log('Données reçues :', data);
    } catch (error) {
        // Gestion des erreurs
        console.error('Erreur :', error);
    }
}

function spectro_fps(fps) {
    fetch(apiUrl+"/api/spectrometer", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ output_sampling_frequency : fps })
      })
}

function spectro_input(spectro_input) {
    fetch(apiUrl+"/api/spectrometer", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ input : spectro_input })
      })
}

// PeakDetect or Average
function spectro_mode(spectro_mode) {
    fetch(apiUrl+"/api/spectrometer", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ mode : spectro_mode })
      })
}

function ddc_frequency(ddc_freq) {
    fetch(apiUrl+"/api/ddc/config", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ frequency : ddc_freq })
      })
}

function ddc_design(ddc_decimation, ddc_stopband) {
    fetch(apiUrl+"/api/ddc/design", {
        method: 'PUT',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ passband_ripple : 0.01, stopband_attenuation_db : ddc_stopband, decimation : ddc_decimation ,
            stopband_one_over_f : true,  transition_bandwidth : 0.05 , frequency : 0 })
      })
}

function ad9361_samplerate(ad_samplerate) {
    fetch(apiUrl+"/api/ad9361", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ sampling_frequency : ad_samplerate })
      })
}

function ad9361_rfbanwidth(ad_bandwidth) {
    fetch(apiUrl+"/api/ad9361", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ rx_rf_bandwidth : ad_bandwidth })
      })
}

function ad9361_gain(ad_gain) {
    fetch(apiUrl+"/api/ad9361", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ rx_gain : ad_gain })
      })
}

function ad9361_gain2(ad_gain) {
    var message = new Paho.MQTT.Message(rfinput.value);
    message.destinationName = "cmd/rx/gain";
    mqtt_client.send(message);
}

function ad9361_gainmode(ad_gainmode) {
    fetch(apiUrl+"/api/ad9361", {
        method: 'PATCH',
        headers: {
          "Content-Type": 'application/json',
        },
        body: JSON.stringify({ rx_gain_mode : ad_gainmode })
      })
}


// Appel de la fonction fetchData pour récupérer les données
//fetchData();

spectro_input("AD9361");//DDC,AD9361
spectro_fps(200);
//ddc_design(32,40);
//ddc_frequency(0);
spectro_mode("Average");// PeakDetect or Average
ad9361_samplerate(60000000);
ad9361_rfbanwidth(56000000);
ad9361_gainmode("Manual");
//ad9361_gain2(Number(50.0));


