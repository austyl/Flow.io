# Profils, cartes, domaines et bootstrap

Cette page décrit la structure actuellement utilisée pour composer un firmware à partir d'un profil, d'une carte et d'un domaine.

Le projet est composé de deux firmwares destinés à deux ESP32 distincts:

- `FlowIO`: nœud principal pour la logique métier et les entrées/sorties
- `Supervisor`: nœud de supervision pour la configuration, le provisioning Wi-Fi, l'écran TFT, les logs et les mises à jour

## Couche `App`

Références principales:

- `src/App/Bootstrap.cpp`
- `src/App/AppContext.h`
- `src/App/FirmwareProfile.h`
- `src/App/BuildFlags.h`

Responsabilités actuelles:

- résolution du profil compilé
- création du contexte global `AppContext`
- exposition du `ModuleManager`, du `ServiceRegistry`, du `ConfigStore` et des préférences NVS
- appel des fonctions `setup` et `loop` du profil sélectionné

Le profil compilé est choisi par les macros:

- `FLOW_PROFILE_FLOWIO`
- `FLOW_PROFILE_SUPERVISOR`

## Couche `Board`

Références principales:

- `src/Board/FlowIOBoardRev1.h`
- `src/Board/SupervisorBoardRev1.h`
- `src/Board/BoardSpec.h`

La couche `Board` porte la description physique des cartes:

- UART disponibles
- bus I2C
- bus 1-Wire
- GPIO et capacités associées
- périphériques locaux du Supervisor

Types utilisés:

- `BoardSignal`
- `IoCapability`
- `IoPointSpec`
- `BoardSpec`
- `SupervisorBoardSpec`

## Couche `Domain`

Références principales:

- `src/Domain/Pool/PoolDomain.h`
- `src/Domain/DomainSpec.h`

Le domaine porte la description logique du produit:

- rôles métier (`DomainRole`)
- association entre signaux de carte et rôles métier
- inventaire des capteurs
- inventaire des équipements
- valeurs par défaut de logique métier

Dans l'état actuel du dépôt:

- `FlowIO` utilise le domaine `Pool`
- `Supervisor` utilise le domaine `Supervisor`

## Couche `Profiles`

Références principales:

- `src/Profiles/FlowIO/*`
- `src/Profiles/Supervisor/*`

Chaque profil assemble:

- un `BoardSpec`
- un `DomainSpec`
- une identité produit
- des instances de modules
- un bootstrap local

Fichiers typiques d'un profil:

- `*Profile.h`: structure `ModuleInstances`
- `*Profile.cpp`: définition du `FirmwareProfile`
- `*Bootstrap.cpp`: ordre d'enregistrement des modules, wiring de profil, post-init

## Sélection par `platformio.ini`

Le fichier `platformio.ini` décrit:

- les environnements compilables
- les macros de profil
- les exclusions de sources via `build_src_filter`
- les scripts de génération lancés avant compilation
- la version de firmware injectée au build

Environnements présents aujourd'hui:

- `FlowIO`
- `FlowIOWokwi`
- `Supervisor`
- `SupervisorWokwi`

## Composition actuelle du profil `FlowIO`

Fichiers de référence:

- `src/Profiles/FlowIO/FlowIOProfile.cpp`
- `src/Profiles/FlowIO/FlowIOProfile.h`
- `src/Profiles/FlowIO/FlowIOBootstrap.cpp`
- `src/Profiles/FlowIO/FlowIOIoLayout.h`
- `src/Profiles/FlowIO/FlowIOIoAssembly.cpp`

Le profil `FlowIO` ajoute aux services communs:

- le module IO
- la logique métier piscine
- les équipements piscine
- le module HMI Nextion
- le serveur de configuration I2C

Le bootstrap `FlowIO` exécute aussi:

- la configuration du module IO à partir du domaine actif
- la définition des équipements `PoolDevice`
- l'enregistrement des providers runtime MQTT
- l'enregistrement des providers Runtime UI sur `i2ccfg.server`

## Composition actuelle du profil `Supervisor`

Fichiers de référence:

- `src/Profiles/Supervisor/SupervisorProfile.cpp`
- `src/Profiles/Supervisor/SupervisorProfile.h`
- `src/Profiles/Supervisor/SupervisorBootstrap.cpp`

Le profil `Supervisor` assemble:

- le client I2C vers `FlowIO`
- le provisioning Wi-Fi
- l'interface web
- la mise à jour de firmware
- l'interface TFT locale

## Couches sollicitées selon le type de modification

| Type de modification | Zone principale |
|---|---|
| changement de broche ou de bus | `src/Board/*` |
| changement de rôle métier | `src/Domain/*` |
| changement des modules présents dans un produit | `src/Profiles/*` et `platformio.ini` |
| changement du profil compilé | `platformio.ini` et `src/App/BuildFlags.h` |
| changement du binding entre rôles et ports IO | `src/Profiles/FlowIO/FlowIOIoLayout.h` et `FlowIOIoAssembly.cpp` |
