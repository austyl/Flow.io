# Remote Display UDP

Le firmware `Display` pilote physiquement le Nextion en UART. Le firmware `FlowIO`
conserve la logique piscine, le modèle HMI et le traitement des `HmiEvent`.
L'échange réseau est un transport UDP binaire léger sur `WiFiUDP`, sans HTTP,
WebSocket ni JSON côté `FlowIO`.

## Profils

- `FlowIO` garde le `NextionDriver` local par défaut.
- `FlowIO` peut utiliser le driver distant avec `hmi/remote_udp/enabled=true`
  ou le define `FLOW_HMI_REMOTE_UDP=1`.
- `Display` contient seulement Wi-Fi, logs série, config minimale,
  `NextionDriver` et `DisplayUdpClientModule`.

## Protocole

Les paquets utilisent `HmiUdpHeader` dans
`src/Core/Hmi/HmiUdpProtocol.h`:

- magic `FH`, version `1`
- port UDP `42110`
- taille maximale `192` octets
- CRC16 Modbus sur l'en-tête sans champ CRC puis le payload

Messages principaux:

- `Hello` en broadcast depuis `Display`
- `Welcome` depuis `FlowIO`
- `Ping` / `Pong` toutes les 2 secondes
- `HomeText`, `HomeGauge`, `HomeStateBits`, `HomeAlarmBits`
- `ConfigStart`, `ConfigRow`, `ConfigEnd`
- `HmiEvent` depuis `Display` vers `FlowIO`
- `RtcWrite`

`HmiEvent` est envoyé avec `ACK_REQUIRED`. `FlowIO` répond `Ack`; `Display`
réessaie après environ 150 ms, jusqu'à 3 tentatives. `FlowIO` ignore un
doublon de séquence déjà traité mais renvoie toujours l'ACK.

## Découverte et lien

Au démarrage, `Display` diffuse `Hello` sur `255.255.255.255:42110`.
`FlowIO` mémorise l'adresse source, répond `Welcome`, puis demande un
rafraîchissement complet de l'écran via le chemin HMI existant.

Si aucun paquet n'est vu pendant environ 9 secondes:

- `FlowIO` marque le display offline.
- `Display` affiche `Connexion Flow.io perdue` via le Nextion et reprend les
  `Hello` broadcast.

## Token

Le token partagé optionnel est configuré dans `hmi/remote_udp/token`.
Il n'est jamais envoyé en clair: `Display` place seulement `tokenCrc` dans
`Hello`. Si le token `FlowIO` est vide, tout `Display` est accepté; sinon le CRC
doit correspondre.

## Limites v1

La lecture RTC distante (`readRtc`) est amorcée par `RtcReadRequest` mais
retourne actuellement `false` côté `RemoteHmiUdpDriver`. L'écriture RTC
(`RtcWrite`) est supportée.
