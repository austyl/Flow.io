# Exposition Runtime UI

Cette note décrit le mécanisme d'exposition runtime `Flow.IO -> Supervisor/UI` utilisé pour lire des valeurs vivantes sans exporter le `DataStore` brut et sans construire de JSON riche côté `Flow.IO`.

## Vue d'ensemble

Le système repose sur 3 briques séparées:
- des annotations manuelles par module (`*RuntimeUi.json`)
- un manifeste JSON global généré pour le `Supervisor` et l'UI web
- un routage runtime binaire compact côté `Flow.IO`

Le manifeste est statique et textuel.
Le routage runtime est dynamique, mais strictement numérique.

## Identité canonique

Chaque valeur runtime exposée est identifiée par:
- `moduleId`
- `valueId`
- `runtimeId = moduleId * 100 + valueId`

Helpers:
- `makeRuntimeUiId(moduleId, valueId)`
- `runtimeUiModuleId(runtimeId)`
- `runtimeUiValueId(runtimeId)`

Important:
- la `key` textuelle (`pool.water_temp`, `wifi.ip`, etc.) est un alias lisible pour le `Supervisor` / l'UI
- l'identité technique canonique est numérique
- le routage `Flow.IO` se fait par `moduleId`, pas par clé textuelle

## Contraintes mémoire

Le design runtime côté `Flow.IO` vise un impact DRAM/heap minimal:
- pas de manifeste JSON en RAM
- pas de cache des valeurs exposées
- pas de duplication du `DataStore`
- pas de `String`, `std::vector`, `std::map`, `DynamicJsonDocument` dans le chemin critique
- pas de `new/delete` pour répondre à une lecture runtime

Le coût mémoire permanent est limité à:
- un registre statique `moduleId -> provider`
- quelques constantes `constexpr` / `const`
- la sérialisation directe dans le buffer de réponse I2C existant

## Composants C++

Le cœur runtime UI est porté par:
- [`RuntimeUi.h`](../../src/Core/RuntimeUi.h)
- [`RuntimeUi.cpp`](../../src/Core/RuntimeUi.cpp)

Composants:
- `IRuntimeUiValueProvider`: interface implémentée par les modules exposants
- `IRuntimeUiWriter`: interface de sérialisation typée
- `RuntimeUiRegistry`: table fixe indexée par `moduleId`
- `RuntimeUiService`: lecture batch d'une liste de `runtimeId`

Le transport binaire supporte:
- `bool`
- `int32`
- `uint32`
- `float`
- `enum`
- `string`
- `not_found`
- `unavailable`

## Pattern pour un module

Chaque module garde ses `valueId` localement dans le code C++.

Pattern attendu:
1. définir les `valueId` locaux dans le module
2. implémenter `writeRuntimeUiValue(valueId, writer)`
3. enregistrer le provider dans `I2CCfgServerModule`
4. documenter les valeurs dans le fichier `*RuntimeUi.json`

Exemple simplifié:

```cpp
class WifiModule : public Module, public IRuntimeUiValueProvider {
public:
    enum RuntimeUiValueId : uint8_t {
        RuntimeUiReady = 1,
        RuntimeUiIp = 2,
        RuntimeUiRssi = 3,
    };

    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }

    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override {
        const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
        switch (valueId) {
            case RuntimeUiReady: return writer.writeBool(runtimeId, wifiReady(*dataStore));
            case RuntimeUiIp: return writer.writeString(runtimeId, "192.168.1.10");
            case RuntimeUiRssi: return writer.writeI32(runtimeId, WiFi.RSSI());
            default: return false;
        }
    }
};
```

## Annotations

Les annotations manuelles vivent dans les modules:
- `src/Modules/**/**RuntimeUi.json`

Format:

```json
{
  "moduleId": "wifi",
  "values": [
    {
      "valueId": 1,
      "key": "wifi.ready",
      "label": "WiFi pret",
      "type": "bool",
      "domain": "wifi",
      "group": "Lien",
      "display": "boolean",
      "order": 10
    }
  ]
}
```

Champs principaux:
- `moduleId`
- `valueId`
- `key`
- `label`
- `type`
- `domain`
- `group` optionnel
- `unit` optionnel
- `decimals` optionnel
- `order` optionnel
- `enum` optionnel
- `flags` optionnel
- `display` optionnel: `circ-gauge`, `horiz-gauge`, `badge`, `boolean`, `time`, `value`
- `displayConfig` optionnel: configuration UI complémentaire, par exemple bornes et bandes d'une jauge, ou libellés d'un booléen

Le champ `display` permet au frontend de choisir un widget générique adapté sans coder les mesures en dur:
- `circ-gauge`: jauge circulaire
- `horiz-gauge`: jauge horizontale
- `badge`: puce texte sans ligne clé/valeur, affichée en tête de card
- `boolean`: tuile d'état booléenne
- `time`: durée stockée en millisecondes, rendue automatiquement en format lisible
- `value`: ligne clé/valeur simple

Pour les jauges circulaires, si `displayConfig.bands` est utilisé avec une configuration 5 zones, les bornes `min/max` de la jauge sont dérivées de `displayConfig.bands.min/max`. Il n'est pas nécessaire de les dupliquer ailleurs.

## Consolidation

Le script [`generate_runtimeui_manifest.py`](../../scripts/generate_runtimeui_manifest.py):
- scanne les fichiers `*RuntimeUi.json`
- valide les modules référencés
- valide l'unicité des `runtimeId`
- vérifie `runtimeId == moduleId * 100 + valueId`
- génère le manifeste JSON global
- génère un petit index C++ pour le `Supervisor`

Sorties:
- [`data/webinterface/runtimeui.json`](../../data/webinterface/runtimeui.json)
- [`src/Core/Generated/RuntimeUiManifest_Generated.h`](../../src/Core/Generated/RuntimeUiManifest_Generated.h)

## Batch runtime

Le `Supervisor` appelle `POST /api/runtime/values` avec une liste d'IDs.

Le client:
- découpe la requête en petits batches adaptés à `I2cCfgProtocol::MaxPayload`
- appelle `OpGetRuntimeUiValues`
- convertit la réponse binaire en JSON homogène

Le serveur `Flow.IO`:
- extrait `moduleId` et `valueId`
- résout le provider dans `RuntimeUiRegistry`
- sérialise directement dans le buffer I2C de réponse

## Valeurs migrées

Les valeurs aujourd'hui exposées par le nouveau système couvrent déjà les usages statut/UI principaux:
- `system`: firmware, uptime, heap libre, heap min
- `wifi`: ready, ip, rssi
- `mqtt`: ready, serveur, compteurs d'erreurs
- `pool`: températures, pH, ORP, modes, états actionneurs

L'ancien chemin `/api/flow/status*` reste disponible pour compatibilité.
Le chemin recommandé pour les lectures runtime nouvelles est `/api/runtime/*`.
