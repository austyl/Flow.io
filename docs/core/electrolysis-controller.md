# Controleur d'electrolyse dedie

Statut: integration `FlowIO` demarree. Le client I2C et le contrat de service existent, mais le firmware du controleur ESP32 dedie reste a implementer.

## Objectif

L'objectif est d'ajouter une fonction d'electrolyse complete sans casser la separation actuelle des responsabilites:

- `FlowIO` conserve la strategie piscine: planning, mode auto, ORP, filtration, Home Assistant, MQTT.
- un troisieme ESP32 dedie devient l'actionneur intelligent d'electrolyse.
- le controleur dedie porte les securites locales: flow switch, temperature, inversion de polarite, temps mort, defauts puissance.

Cette separation evite de faire piloter directement l'IBT-2 depuis `PoolLogicModule` et garde le firmware principal dans son role d'orchestrateur.

## Repartition des responsabilites

```text
FlowIO
  - decide si l'electrolyse est demandee
  - calcule la consigne a partir du mode auto, de la filtration et de l'ORP
  - expose configuration, runtime, MQTT et Home Assistant

ESP32 Electrolyse
  - recoit une consigne simple via I2C
  - verifie les securites locales a chaque cycle
  - pilote l'IBT-2 avec deadtime obligatoire
  - inverse la polarite selon la periode configuree
  - coupe automatiquement en defaut ou perte de communication

IBT-2 / cellule
  - ne recoit jamais deux commandes PWM opposees simultanees
  - reste eteint pendant les phases de deadtime et de defaut
```

## Principe d'integration avec l'architecture actuelle

Le chemin metier cible reste proche du chemin existant:

```text
PoolLogicModule
  -> demande "electrolyse ON/OFF" selon ORP, filtration, temperature minimale et delais
PoolDeviceModule
  -> conserve le slot pd5 "Chlorine Generator" pour enable, interlocks, uptime et exposition runtime
Electrolysis bridge / driver FlowIO
  -> transmet la consigne au controleur dedie
ESP32 Electrolyse
  -> execute physiquement la production et les inversions
```

`PoolLogicModule` ne doit pas manipuler de GPIO, de PWM ou de polarite. Il reste consommateur de services, comme aujourd'hui avec `PoolDeviceService`.

`PoolDeviceModule` peut rester la couche de comptage et d'interlock general. L'electrolyse dediee ajoute une securite plus proche du materiel, mais ne remplace pas les regles metier globales.

## Liaison I2C

La liaison I2C sert uniquement aux consignes et au retour d'etat. Elle ne doit pas etre la seule protection de securite.

Hypothese recommandee:

- le controleur d'electrolyse est dans le meme coffret ou sur une liaison courte;
- le controleur est esclave I2C;
- `FlowIO` est maitre I2C;
- un heartbeat est envoye periodiquement par `FlowIO`;
- si le heartbeat expire, le controleur coupe l'IBT-2.

Adresse a privilegier: `0x43` ou `0x45`, sous reserve d'absence de conflit avec les peripheriques deja presents.

Adresses a eviter par defaut:

- `0x40`: INA226 courant/tension possible
- `0x42`: protocole `FlowIO` / `Supervisor`
- `0x44`: SHT40 possible
- `0x48`: ADS1115 interne
- `0x76` / `0x77`: BMP/BME possibles

Si la liaison doit traverser un cable long ou un environnement tres bruite, RS485 ou CAN serait preferable a I2C. I2C reste acceptable dans le meme coffret avec pull-ups et cablage courts.

## Utilisation des entrees I2C disponibles

Avec 5 connecteurs I2C encore disponibles, la recommandation est de n'en consommer qu'un seul cote `FlowIO` pour parler au controleur d'electrolyse dedie.

Les capteurs critiques de l'electrolyse devraient rester locaux au troisieme ESP32:

- flow switch cable directement sur l'ESP32 electrolyse;
- temperature cellule/eau locale si elle est dediee a la securite electrolyse;
- mesure courant/tension cellule sur le bus local de l'ESP32 electrolyse si un INA219/INA226 est utilise;
- entree defaut alimentation ou temperature radiateur IBT-2 locale.

`FlowIO` garde donc ses autres connecteurs I2C pour les extensions piscine generales. Le bus principal ne transporte que la consigne et l'etat synthetique de l'electrolyse, pas toutes les mesures rapides ou critiques.

## Raccordement sur PoolMaster ExtendedBoard

L'image `docs/ExtendedBoard_PCB.png` du projet PoolMaster historique confirme:

- deux emplacements ESP32 sur l'ExtendedBoard: `U1` pour le controleur principal et `U9` pour le watchdog/maintenance;
- une zone `Extension Ports` en bas a droite;
- dix connecteurs `I2C-5V` d'apres la BOM: `K20`, `K21`, `K22`, `K23`, `K24`, `K30`, `K31`, `K32`, `K33`, `K34`.

Recommandation materielle:

- garder `U9` dans son role watchdog/maintenance si cette fonction est conservee;
- raccorder le controleur electrolyse comme troisieme ESP32 externe via un seul connecteur I2C libre;
- utiliser une adresse dediee, par defaut `0x43`;
- ne pas raccorder directement une ligne I2C 5 V sur les GPIO ESP32: verifier si le connecteur choisi est deja translate en 3.3 V, sinon ajouter un level shifter I2C;
- alimenter l'ESP32 electrolyse par son entree `5V/VIN` si le port fournit du 5 V, jamais par une broche `3V3` non prevue pour cela;
- garder les liaisons I2C courtes dans le coffret, avec masse commune.

Le connecteur I2C choisi ne doit transporter que `SDA`, `SCL`, `GND` et eventuellement l'alimentation logique du controleur. Les sorties IBT-2, flow switch, temperature locale et mesure courant/tension doivent rester cablees sur l'ESP32 electrolyse.

`U9` pourrait techniquement etre detourne pour l'electrolyse si le watchdog n'est pas utilise, mais ce n'est pas l'option recommandee: on melangerait supervision generale et actionneur de puissance, exactement ce qu'on cherche a eviter.

## Enseignements du PoolMaster historique

Le dossier `esp32-poolmaster-electrolyse-` et le schema `PoolMaster 2.PDF` montrent surtout une carte principale/extension autour d'un ESP32, PCF8574, ADS1115, bus I2C et connecteurs I2C multiples. Le PDF ne montre pas directement l'IBT-2, mais le firmware historique contient deja une logique experimentale d'electrolyse.

Points utiles a conserver:

- la regulation ORP calcule une demande de production `0..100%`;
- la commande physique de la cellule est separee de la regulation ORP;
- la production est appliquee sur une fenetre ON/OFF longue, par defaut `300 s`, plutot qu'une obligation de PWM rapide;
- la production attend un delai apres demarrage filtration, par defaut `30 s`;
- l'inversion de polarite utilise une periode par defaut de `180 min`;
- le temps mort avant inversion est de `5 s`;
- la temperature minimale d'eau est de `15.0 C`;
- la pression/debit et le flow switch sont traites comme securites de production.

Pour `FlowIO`, on garde l'idee la plus solide: `PoolLogicModule` calcule une demande de production, tandis que le controleur dedie applique cette demande avec ses propres securites locales.

## Commande I2C proposee

Commande envoyee par `FlowIO` au controleur dedie.
Le contrat binaire correspondant est declare dans `include/Core/ElectrolysisProtocol.h`.

| Champ | Type | Description |
| --- | --- | --- |
| `version` | `uint8` | Version du protocole |
| `seq` | `uint8` | Compteur incrementant pour detection de trames nouvelles |
| `control_flags` | `uint8` | Bits de controle, dont reset des defauts latched |
| `enable` | `bool` | Demande de production |
| `production_pct` | `uint8` | Production demandee, `0..100` |
| `start_delay_s` | `uint16` | Delai local apres demande valide avant production |
| `production_window_s` | `uint16` | Fenetre ON/OFF locale pour appliquer `production_pct` |
| `reverse_period_min` | `uint16` | Periode d'inversion de polarite |
| `deadtime_ms` | `uint16` | Temps mort avant changement de polarite |
| `min_water_temp_c10` | `int16` | Temperature minimale en dixiemes de degre |
| `max_current_ma` | `uint16` | Courant cellule maximum autorise |
| `heartbeat` | `uint8` | Compteur de vie emis par `FlowIO` |
| `crc8` | `uint8` | Controle simple de trame |

La commande doit etre idempotente: renvoyer deux fois la meme consigne ne doit pas provoquer deux inversions ou deux impulsions.

## Etat I2C propose

Etat lu par `FlowIO`.
Le contrat binaire correspondant est declare dans `include/Core/ElectrolysisProtocol.h`.

| Champ | Type | Description |
| --- | --- | --- |
| `version` | `uint8` | Version du protocole |
| `seq_ack` | `uint8` | Derniere sequence traitee |
| `state` | `uint8` | Etat courant de la machine d'etat |
| `fault_mask` | `uint16` | Defauts actifs |
| `flow_ok` | `bool` | Etat flow switch filtre/debounce |
| `temp_c10` | `int16` | Temperature locale en dixiemes de degre |
| `current_ma` | `uint16` | Courant cellule mesure |
| `voltage_mv` | `uint16` | Tension cellule mesuree |
| `polarity` | `int8` | `-1`, `0`, `+1` |
| `production_applied_pct` | `uint8` | Production reellement appliquee pendant la fenetre locale |
| `last_reverse_s` | `uint32` | Temps depuis derniere inversion |
| `uptime_s` | `uint32` | Temps de production cumule local |
| `crc8` | `uint8` | Controle simple de trame |

## Machine d'etat locale

Etats proposes cote ESP32 dedie:

| Etat | Role |
| --- | --- |
| `Idle` | IBT-2 coupe, aucune production demandee |
| `WaitFlow` | Production demandee mais debit absent |
| `WaitTemp` | Production demandee mais temperature hors plage |
| `Starting` | Debut de production apres validation securites |
| `RunningForward` | Production polarite positive |
| `RunningReverse` | Production polarite negative |
| `DeadtimeBeforeReverse` | IBT-2 coupe avant inversion |
| `Stopping` | Arret commande, sortie ramenee a zero |
| `Fault` | Defaut latche ou critique, sortie coupee |

Transitions de securite prioritaires:

- perte flow switch -> coupure immediate;
- temperature hors plage -> coupure immediate;
- surintensite -> coupure immediate et defaut latche;
- perte heartbeat -> coupure immediate;
- trame I2C invalide repetee -> coupure immediate;
- redemarrage local -> sortie coupee par defaut jusqu'a nouvelle consigne valide.

## Pilotage IBT-2

Le pilotage IBT-2 doit respecter une sequence stricte:

1. mettre `RPWM=0` et `LPWM=0`;
2. desactiver les enables si disponibles;
3. attendre `deadtime_ms`;
4. selectionner la polarite cible;
5. reactiver les enables;
6. appliquer la PWM sur un seul cote.

Convention recommandee:

| Polarite | `RPWM` | `LPWM` | `R_EN` | `L_EN` |
| --- | --- | --- | --- | --- |
| Off | `0` | `0` | `0` | `0` |
| Forward | PWM | `0` | `1` | `1` |
| Reverse | `0` | PWM | `1` | `1` |
| Deadtime | `0` | `0` | `0` | `0` |

Le controleur dedie doit garantir en logiciel que `RPWM` et `LPWM` ne sont jamais actifs ensemble.

Pour une premiere version, `production_pct` devrait piloter une fenetre ON/OFF longue:

- `production_pct = 0`: cellule toujours coupee;
- `production_pct = 50` avec `production_window_s = 300`: environ `150 s` ON puis `150 s` OFF;
- `production_pct = 100`: cellule active en continu tant que les securites restent bonnes.

Une PWM LEDC rapide sur l'IBT-2 peut rester possible plus tard, mais elle ne doit pas etre l'hypothese de base sans validation de l'alimentation cellule, du courant et de l'echauffement.

## Securites locales minimales

Securites conseillees des la premiere implementation:

- flow switch obligatoire pour produire;
- temperature minimale obligatoire;
- watchdog heartbeat I2C;
- delai local apres demarrage/demande avant activation cellule;
- fenetre ON/OFF locale pour appliquer le pourcentage de production;
- deadtime materiel avant inversion;
- etat off au boot;
- defaut latche sur surintensite si une mesure courant est presente;
- seuil de courant configurable mais borne par une limite compile-time raisonnable.

Securites optionnelles mais recommandees:

- mesure tension cellule;
- temperature radiateur IBT-2;
- entree defaut alimentation;
- relais ou contacteur d'alimentation cellule separe du PWM;
- journal d'erreur local expose dans l'etat I2C.

## Integration firmware future

L'integration cote `FlowIO` commence par les briques suivantes:

1. `include/Core/ElectrolysisProtocol.h` declare le contrat I2C partage;
2. `src/Core/Services/IElectrolysis.h` declare le service interne optionnel;
3. `src/Modules/ElectrolysisModule` implemente le client I2C, desactive par defaut;
4. `IOServiceV2::i2cTransfer` partage le bus IO et son mutex avec les capteurs existants;
5. `PoolLogicModule` envoie une demande au service electrolysis si celui-ci est present et disponible;
6. si le module electrolysis est desactive, le comportement historique via `PoolDeviceModule` reste actif;
7. si le module electrolysis est active, `PoolLogicModule` garde le relais `pd5` a l'arret et delegue la production au controleur distant.

Les etapes suivantes seront:

1. exposer runtime MQTT `rt/electrolysis/state`;
2. relier les alarmes `no_flow`, `temp_block`, `over_current`, `comm_lost`;
3. faire remonter l'etat du controleur dedie vers l'UI et Home Assistant;
4. ajouter le firmware du troisieme ESP32.

L'implementation cote ESP32 dedie peut demarrer comme un firmware separe minimal:

1. initialisation sorties IBT-2 a zero;
2. lecture flow switch et temperature;
3. esclave I2C avec registres commande/etat;
4. machine d'etat locale;
5. pilotage IBT-2 avec inversion, deadtime et fenetre de production;
6. watchdog communication.

## Decisions restant a prendre

- Adresse I2C definitive.
- Brochage ESP32 dedie: `RPWM`, `LPWM`, `R_EN`, `L_EN`, flow switch, temperature, courant/tension.
- Type de capteur temperature local: DS18B20, NTC, capteur I2C.
- Type de mesure courant/tension: INA226, INA219, capteur Hall, autre.
- Strategie electrique exacte: ON/OFF longue, PWM LEDC, ou regulation de courant dediee.
- Periode d'inversion par defaut.
- Strategie defaut: latch manuel obligatoire ou clear automatique pour certains defauts.
- Choix d'integration FlowIO: module client dedie ou extension du chemin `PoolDeviceModule`.
