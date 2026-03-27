# Mise en service matérielle et flash

Cette page s'adresse à l'intégrateur qui veut câbler la carte, compiler le bon firmware et vérifier le démarrage sans modifier l'architecture du projet.

## 1. Choisir le firmware

Le dépôt produit deux binaires distincts:

| Firmware | Environnement PlatformIO | Usage |
|---|---|---|
| `FlowIO` | `FlowIO` | carte principale avec E/S, logique métier, MQTT, Home Assistant, Nextion |
| `Supervisor` | `Supervisor` | carte d'interface locale avec TFT, provisioning, mise à jour et bus I2C vers `FlowIO` |

La sélection se fait dans `platformio.ini`:

- `default_envs = FlowIO`
- environnement `env:FlowIO`
- environnement `env:Supervisor`

## 2. Compiler et flasher

Commandes usuelles:

```sh
~/.platformio/penv/bin/pio run -e FlowIO
~/.platformio/penv/bin/pio run -e FlowIO -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

```sh
~/.platformio/penv/bin/pio run -e Supervisor
~/.platformio/penv/bin/pio run -e Supervisor -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Scripts de pré-build actuellement exécutés par `platformio.ini`:

- `scripts/generate_build_version.py`
- `scripts/generate_datamodel.py`
- `scripts/generate_runtimeui_manifest.py`
- `scripts/generate_config_docs.py`

## 3. Câblage `FlowIO`

Références:

- `src/Board/FlowIOBoardRev1.h`
- `src/Board/BoardSerialMap.h`
- `src/Modules/Network/I2CCfgServerModule/I2CCfgServerModule.h`

### Sorties digitales

| Fonction actuelle | GPIO |
|---|---:|
| `relay1` | 32 |
| `relay2` | 25 |
| `relay3` | 26 |
| `relay4` | 13 |
| `relay5` | 33 |
| `relay6` | 27 |
| `relay7` | 23 |
| `relay8` | 4 |

### Entrées digitales

| Fonction actuelle | GPIO |
|---|---:|
| `digital_in1` | 34 |
| `digital_in2` | 36 |
| `digital_in3` | 39 |
| `digital_in4` | 35 |

### Bus et interfaces

| Interface | GPIO |
|---|---|
| I2C `io` | SDA 21, SCL 22 |
| 1-Wire `temp_probe_1` | 19 |
| 1-Wire `temp_probe_2` | 18 |
| Nextion UART2 | RX 16, TX 17 |
| Console série UART0 | TX 1, RX 3 |
| I2C interlink par défaut | SDA 12, SCL 14 |

Le port Nextion peut être échangé avec le port de logs via la macro `FLOW_SWAP_LOG_HMI_SERIAL`.

## 4. Câblage `Supervisor`

Références:

- `src/Board/SupervisorBoardRev1.h`
- `src/Profiles/Supervisor/SupervisorProfile.cpp`
- `src/Modules/Network/I2CCfgClientModule/I2CCfgClientModule.h`

### TFT ST7789

| Signal | GPIO |
|---|---:|
| Backlight | 14 |
| CS | 15 |
| DC | 2 |
| RST | 4 |
| MOSI | 23 |
| SCLK | 18 |

### Nextion et pont série

| Interface | GPIO |
|---|---|
| UART `bridge` vers `FlowIO` | RX 16, TX 17 |
| UART `panel` Nextion | RX 33, TX 32 |
| reboot matériel Nextion | 13 |

### Interlink et pilotage du `FlowIO`

| Fonction | GPIO |
|---|---:|
| I2C interlink SDA | 21 |
| I2C interlink SCL | 22 |
| `flowIoEnablePin` | 25 |
| `flowIoBootPin` | 26 |
| PIR écran | 36 |

Valeurs runtime actuelles du profil Supervisor:

- extinction backlight: `10000 ms`
- appui long reset Wi-Fi: `3000 ms`

## 5. Vérifications au premier démarrage

### `FlowIO`

Vérifier dans les logs:

- initialisation du profil `FlowIO`
- démarrage des modules `wifi`, `time`, `mqtt`, `io`, `poollogic`, `pooldev`
- présence des topics `status`, `rt/network/state`, `rt/system/state`
- si écran Nextion branché, activité du module `hmi`

### `Supervisor`

Vérifier dans les logs:

- initialisation du profil `Supervisor`
- démarrage des modules `wifi`, `wifiprov`, `i2ccfg.client`, `fwupdate`, `hmi.supervisor`
- détection du lien I2C vers `FlowIO`
- affichage TFT et gestion du rétroéclairage

## 6. Modules activés par profil

La présence d'un module dépend de trois points:

1. l'environnement compilé dans `platformio.ini`
2. les champs présents dans `src/Profiles/<Profil>/<Profil>Profile.h`
3. l'enregistrement du module dans `src/Profiles/<Profil>/<Profil>Bootstrap.cpp`

Le détail des fichiers à modifier est décrit dans [Adapter le projet à un autre domaine](adaptation-domaine.md).
