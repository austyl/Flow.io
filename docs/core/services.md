# Services Core et architecture ID-based

Toutes les interfaces de service vivent sous `src/Core/Services/` et sont agrégées par `src/Core/Services/Services.h`.

L'architecture actuelle n'utilise plus un registre basé sur des clés chaîne comme API primaire. Le point d'entrée officiel est maintenant :

- `ServiceId` dans `src/Core/ServiceId.h`
- `ServiceRegistry` dans `src/Core/ServiceRegistry.h`
- `ServiceBinding` dans `src/Core/ServiceBinding.h`

## Principe général

Un service est un contrat C simple :

- un `struct` de pointeurs de fonctions
- un champ `void* ctx` pour l'instance porteuse
- parfois, pour les wrappers noyau, un simple pointeur direct (`DataStore*`, `EventBus*`)

Le registre est **ID-based** :

- chaque service est enregistré sous un `ServiceId`
- les IDs sont stables, compacts, et centralisés dans `src/Core/ServiceId.h`
- `ServiceRegistry` stocke un slot par ID, sans lookup par chaîne à l'exécution

## Consommer un service

Exemple d'accès dans `init()` :

```cpp
#include "Core/ServiceId.h"
#include "Core/Services/Services.h"

void MyModule::init(ConfigStore&, ServiceRegistry& services)
{
    const IOServiceV2* io = services.get<IOServiceV2>(ServiceId::Io);
    const TimeService* time = services.get<TimeService>(ServiceId::Time);
    if (!io || !time) return;
}
```

Notes :

- `services.get<T>(id)` est un cast typé sur un slot `void*`
- le couple `ServiceId` + type attendu doit être cohérent
- `get()` retourne `nullptr` si le service n'est pas enregistré

## Exposer un service

Pattern recommandé :

- déclarer le service comme membre `svc_` du module
- binder directement les méthodes d'instance via `ServiceBinding::bind<&MyModule::method_>`
- utiliser `ServiceBinding::bind_or<&MyModule::method_, fallback>` seulement pour les retours non-`void` qui ont un défaut métier explicite
- si la signature métier ne colle pas exactement au contrat, ajouter une petite méthode d'adaptation `xxxSvc_()`

Exemple :

```cpp
#include "Core/ServiceBinding.h"
#include "Core/ServiceId.h"

class MyModule : public Module {
private:
    bool doThing_(int value);

    MyService svc_{
        ServiceBinding::bind<&MyModule::doThing_>,
        this
    };
};

void MyModule::init(ConfigStore&, ServiceRegistry& services)
{
    (void)services.add(kMyServiceId, &svc_);
}
```

`kMyServiceId` représente ici l'ID réellement réservé à ce contrat.

Contraintes `ServiceBinding` :

- supporte les méthodes `const` et non-`const`
- ne supporte pas les retours par référence
- `bind_or` ne supporte pas les méthodes `void`
- si `ctx == nullptr`, `bind` retourne une valeur par défaut C++ (`R{}`) ou no-op pour `void`

## Règles du `ServiceRegistry`

Le registre actuel est strict :

- `add()` refuse un `ServiceId` invalide
- `add()` refuse un pointeur nul
- `add()` refuse les doublons
- `has()` teste simplement la présence d'un slot
- `getRaw()` et `get<T>()` retournent `nullptr` si l'ID n'est pas valide ou absent

Autrement dit, un service n'est pas censé être réenregistré ou remplacé dynamiquement pendant le cycle de vie normal des modules.

## Inventaire des `ServiceId`

`src/Core/ServiceId.h` est la source de vérité. Les noms texte ci-dessous servent surtout au debug et à la compatibilité documentaire via `toString(ServiceId)`.

| `ServiceId` | Nom texte | Contrat |
| --- | --- | --- |
| `LogHub` | `loghub` | `LogHubService` |
| `LogSinks` | `logsinks` | `LogSinkRegistryService` |
| `EventBus` | `eventbus` | `EventBusService` |
| `ConfigStore` | `config` | `ConfigStoreService` |
| `DataStore` | `datastore` | `DataStoreService` |
| `Command` | `cmd` | `CommandService` |
| `Alarm` | `alarms` | `AlarmService` |
| `Hmi` | `hmi` | `HmiService` |
| `Wifi` | `wifi` | `WifiService` |
| `Time` | `time` | `TimeService` |
| `TimeScheduler` | `time.scheduler` | `TimeSchedulerService` |
| `Mqtt` | `mqtt` | `MqttService` |
| `Ha` | `ha` | `HAService` |
| `Io` | `io` | `IOServiceV2` |
| `StatusLeds` | `status_leds` | `StatusLedsService` |
| `PoolDevice` | `pooldev` | `PoolDeviceService` |
| `WebInterface` | `webinterface` | `WebInterfaceService` |
| `FirmwareUpdate` | `fwupdate` | `FirmwareUpdateService` |
| `NetworkAccess` | `network_access` | `NetworkAccessService` |
| `FlowCfg` | `flowcfg` | `FlowCfgRemoteService` |

La présence effective d'un service dépend du profil compilé et des modules activés.

## Contrats principaux

### `LogHubService`

- enqueue non bloquant d'un `LogEntry`
- enregistrement des noms de modules de log
- filtrage par niveau (`shouldLog`, `setModuleMinLevel`, `getModuleMinLevel`)
- métriques de hub (`getStats`, `noteFormatTruncation`)

### `LogSinkRegistryService`

- enregistrement des sinks (`add`)
- itération (`count`, `get`)

### `CommandService`

- enregistrement de handlers (`registerHandler`)
- exécution (`execute(cmd, json, args, reply, replyLen)`)
- les handlers reçoivent un `CommandRequest`

### `ConfigStoreService`

- import JSON (`applyJson`)
- export global (`toJson`)
- export module (`toJsonModule`)
- liste modules (`listModules`)
- effacement complet (`erase`)

### `DataStoreService`

- expose directement `DataStore* store`
- utilisé par les helpers runtime et les modules métiers

### `EventBusService`

- expose directement `EventBus* bus`
- abonnement/publication via l'API native `EventBus`

### `AlarmService`

- enregistrement d'alarmes (`registerAlarm`)
- acquittement (`ack`, `ackAll`)
- lecture d'état (`isActive`, `isAcked`, `activeCount`, `highestSeverity`)
- snapshots (`buildSnapshot`, `listIds`, `buildAlarmState`, `buildPacked`)

### `HmiService`

- refresh d'affichage (`requestRefresh`)
- navigation config (`openConfigHome`, `openConfigModule`)
- snapshot menu (`buildConfigMenuJson`)
- gestion page LEDs (`setLedPage`, `getLedPage`)

### `WifiService`

- état (`state`, `isConnected`)
- IP (`getIP`)
- actions runtime (`requestReconnect`, `requestScan`)
- télémétrie scan (`scanStatusJson`)
- contrôle retry STA (`setStaRetryEnabled`)

### `TimeService`

- état de synchro (`state`, `isSynced`)
- epoch (`epoch`)
- formatage local (`formatLocalTime`)

### `TimeSchedulerService`

- gestion des slots (`setSlot`, `getSlot`, `clearSlot`, `clearAll`)
- observation (`usedCount`, `activeMask`, `isActive`)
- supporte les modes `RecurringClock` et `OneShotEpoch`

### `MqttService`

- enqueue job (`enqueue(producerId, messageId, prio, flags)`)
- enregistrement de producteur (`registerProducer`)
- format de topic (`formatTopic`)
- état transport (`isConnected`)

Le service MQTT est **job-based** :

- le contrat public ne prend pas directement `topic/payload`
- chaque producteur possède son mapping local `messageId -> build`
- le payload final est construit au plus tard via `MqttBuildContext`
- `enqueue()` peut refuser si le transport n'est pas prêt ou si la queue ne peut pas accepter le job

### `HAService`

- enregistrement statique d'entités discovery via :
- `addSensor`
- `addBinarySensor`
- `addSwitch`
- `addNumber`
- `addButton`
- demande de refresh (`requestRefresh`)

### `IOServiceV2`

- inventaire endpoints (`count`, `idAt`, `meta`)
- lecture typée générique (`readValue`)
- digital (`readDigital`, `writeDigital`)
- analog (`readAnalog`)
- cycle IO (`tick`, `lastCycle`)

L'accès cross-module aux IO doit passer par `IoId`, jamais par des noms libres.

### `StatusLedsService`

- écriture masque logique (`setMask`)
- lecture masque courant (`getMask`)

### `PoolDeviceService`

- inventaire slots (`count`, `meta`)
- lecture état réel (`readActualOn`)
- commande état désiré (`writeDesired`)
- refill tank (`refillTank`)

### `NetworkAccessService`

- reachability web (`isWebReachable`)
- mode réseau (`mode`)
- IP active (`getIP`)
- notification de changement Wi-Fi (`notifyWifiConfigChanged`)

### `WebInterfaceService`

- pause/reprise interface web (`setPaused`)
- état (`isPaused`)

### `FirmwareUpdateService`

- démarrage mise à jour (`start`)
- état JSON (`statusJson`)
- config JSON (`configJson`)
- mise à jour de config source (`setConfig`)

### `FlowCfgRemoteService`

- readiness (`isReady`)
- navigation config distante (`listModulesJson`, `listChildrenJson`, `getModuleJson`)
- snapshots runtime (`runtimeStatusDomainJson`, `runtimeStatusJson`, `runtimeAlarmSnapshotJson`)
- lecture binaire Runtime UI (`runtimeUiValues`)
- application patch JSON (`applyPatchJson`)

Ce service est le contrat supervisor/I2C pour piloter un nœud Flow.IO distant.

## Migration depuis l'ancien modèle

Ancienne forme :

```cpp
services.get<IOServiceV2>("io");
services.add("mqtt", &svc_);
```

Forme actuelle :

```cpp
services.get<IOServiceV2>(ServiceId::Io);
services.add(ServiceId::Mqtt, &svc_);
```

Les chaînes ne sont plus l'API de lookup du `ServiceRegistry`. Si un nom texte est encore nécessaire pour du debug, utiliser `toString(ServiceId)`.

## Bonnes pratiques

- récupérer les dépendances en `init()` ou `onConfigLoaded()` et stocker le pointeur typé
- toujours vérifier `nullptr` avant usage
- préférer des contrats étroits et stables à des services “génériques”
- éviter allocations dynamiques et parsing coûteux dans les callbacks de service critiques
- garder les `ServiceId` comme source unique de wiring inter-modules
- documenter tout nouveau service dans `src/Core/ServiceId.h`, `src/Core/Services/` et cette page
