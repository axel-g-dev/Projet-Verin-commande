// Mise à jour en temps réel de l'affichage du slider
document.getElementById('consigne').addEventListener('input', function() {
    var valeur = (this.value / 10).toFixed(1);
    document.getElementById('consigne_affichee').textContent = valeur;
});

// ---- AFFICHAGE POSITION ----
setInterval(AfficherVariable, 1000);

function AfficherVariable() {
    var reqVariable = new XMLHttpRequest();

    reqVariable.onreadystatechange = function() {
        if (reqVariable.readyState == 4 && reqVariable.status == 200) {
            document.getElementById("id_Distance_mesure").innerHTML = this.responseText;
        }
    };

    reqVariable.open("GET", "/position", true);
    reqVariable.send();
}

// ---- ENVOI CONSIGNE ----
function envoyerConsigne() {
    var sliderValue = document.getElementById("consigne").value;
    var valeur = (sliderValue / 10).toFixed(1);

    var req = new XMLHttpRequest();
    req.open("POST", "/setConsigne", true);
    req.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");

    req.send("c=" + valeur);
}

// Initialisation au chargement
AfficherVariable();