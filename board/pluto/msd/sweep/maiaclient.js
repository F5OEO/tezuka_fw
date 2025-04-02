// URL de l'API REST
const apiUrl = "/api";

// Fonction pour appeler l'API REST
async function fetchData() {
    try {
        // Appel à l'API REST en utilisant fetch
        const response = await fetch(apiUrl, {
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

// Appel de la fonction fetchData pour récupérer les données
fetchData();
