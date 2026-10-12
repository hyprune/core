#pragma once
namespace hyprune::world {
inline constexpr const char *worldSchema = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0/world.schema.json",
  "title": "Hyprune v0 world",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.1"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0/common.schema.json",
  "title": "Hyprune v0 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    }
  }
})schema";
inline constexpr const char *worldSchema02 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.2/world.schema.json",
  "title": "Hyprune 0.2 world",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.2"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema02 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.2/common.schema.json",
  "title": "Hyprune 0.2 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    }
  }
})schema";
inline constexpr const char *commonSchema03 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.3/common.schema.json",
  "title": "Hyprune 0.3 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *worldSchema03 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.3/world.schema.json",
  "title": "Hyprune 0.2 world",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.3"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/area"
      },
      "maxItems": 32
    },
    "atmosphere": {
      "$ref": "common.schema.json#/$defs/atmosphere"
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema04 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.4/common.schema.json",
  "title": "Hyprune 0.3 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *worldSchema04 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.4/world.schema.json",
  "title": "Hyprune world 0.4: climbable volumes",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.4"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/area"
      },
      "maxItems": 32
    },
    "atmosphere": {
      "$ref": "common.schema.json#/$defs/atmosphere"
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *worldSchema05 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.5/world.schema.json",
  "title": "Hyprune world 0.5: ink and visibility areas",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.5"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/area"
      },
      "maxItems": 32
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        },
        "outline": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "colour": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 3,
              "maxItems": 3
            },
            "widthPx": {
              "type": "number",
              "minimum": 1,
              "maximum": 4
            },
            "depthThreshold": {
              "type": "number",
              "minimum": 0.0001,
              "maximum": 1
            },
            "normalThreshold": {
              "type": "number",
              "minimum": 0.001,
              "maximum": 2
            }
          },
          "required": [
            "enabled",
            "colour",
            "widthPx",
            "depthThreshold",
            "normalThreshold"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    },
    "visibility": {
      "type": "object",
      "properties": {
        "areas": {
          "type": "array",
          "maxItems": 256,
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "bounds": {
                "type": "object",
                "properties": {
                  "min": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "max": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  }
                },
                "required": [
                  "min",
                  "max"
                ],
                "additionalProperties": false
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9999
                },
                "maxItems": 10000,
                "uniqueItems": true
              },
              "visibleFrom": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "maxItems": 256,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "bounds",
              "nodes",
              "visibleFrom"
            ],
            "additionalProperties": false
          }
        }
      },
      "required": [
        "areas"
      ],
      "additionalProperties": false
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *worldSchema06 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.6/world.schema.json",
  "title": "Hyprune world 0.6: playable bounds and backdrop declarations",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.6"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "workspaceSlot": {
            "type": "string",
            "pattern": "^[0-9]{2,4}$"
          },
          "primaryAnchor": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "toolAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "dataAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "theme": {
            "type": "string",
            "maxLength": 256
          },
          "spawnId": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "freePlacementZones": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 32
          },
          "playable": {
            "type": "object",
            "additionalProperties": false,
            "anyOf": [
              {
                "required": [
                  "volumes"
                ],
                "properties": {
                  "volumes": {}
                }
              },
              {
                "required": [
                  "maxFlyHeight"
                ],
                "properties": {
                  "maxFlyHeight": {}
                }
              }
            ],
            "properties": {
              "volumes": {
                "type": "array",
                "minItems": 1,
                "maxItems": 8,
                "items": {
                  "anyOf": [
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "box"
                        },
                        "min": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        },
                        "max": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        }
                      }
                    },
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "points"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "convex"
                        },
                        "points": {
                          "type": "array",
                          "minItems": 4,
                          "maxItems": 16,
                          "uniqueItems": true,
                          "items": {
                            "type": "array",
                            "minItems": 3,
                            "maxItems": 3,
                            "items": {
                              "type": "number",
                              "minimum": -100000,
                              "maximum": 100000
                            }
                          }
                        }
                      }
                    }
                  ]
                }
              },
              "maxFlyHeight": {
                "type": "number",
                "minimum": -100000,
                "maximum": 100000
              }
            },
            "required": [
              "volumes"
            ]
          }
        },
        "required": [
          "id",
          "workspaceSlot",
          "primaryAnchor",
          "toolAnchors",
          "dataAnchors",
          "theme"
        ],
        "additionalProperties": false
      },
      "maxItems": 32
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        },
        "outline": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "colour": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 3,
              "maxItems": 3
            },
            "widthPx": {
              "type": "number",
              "minimum": 1,
              "maximum": 4
            },
            "depthThreshold": {
              "type": "number",
              "minimum": 0.0001,
              "maximum": 1
            },
            "normalThreshold": {
              "type": "number",
              "minimum": 0.001,
              "maximum": 2
            }
          },
          "required": [
            "enabled",
            "colour",
            "widthPx",
            "depthThreshold",
            "normalThreshold"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    },
    "visibility": {
      "type": "object",
      "properties": {
        "areas": {
          "type": "array",
          "maxItems": 256,
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "bounds": {
                "type": "object",
                "properties": {
                  "min": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "max": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  }
                },
                "required": [
                  "min",
                  "max"
                ],
                "additionalProperties": false
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9999
                },
                "maxItems": 10000,
                "uniqueItems": true
              },
              "visibleFrom": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "maxItems": 256,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "bounds",
              "nodes",
              "visibleFrom"
            ],
            "additionalProperties": false
          }
        }
      },
      "required": [
        "areas"
      ],
      "additionalProperties": false
    },
    "playable": {
      "type": "object",
      "additionalProperties": false,
      "anyOf": [
        {
          "required": [
            "volumes"
          ],
          "properties": {
            "volumes": {}
          }
        },
        {
          "required": [
            "maxFlyHeight"
          ],
          "properties": {
            "maxFlyHeight": {}
          }
        }
      ],
      "properties": {
        "volumes": {
          "type": "array",
          "minItems": 1,
          "maxItems": 8,
          "items": {
            "anyOf": [
              {
                "type": "object",
                "required": [
                  "type",
                  "min",
                  "max"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "box"
                  },
                  "min": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  },
                  "max": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  }
                }
              },
              {
                "type": "object",
                "required": [
                  "type",
                  "points"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "convex"
                  },
                  "points": {
                    "type": "array",
                    "minItems": 4,
                    "maxItems": 16,
                    "uniqueItems": true,
                    "items": {
                      "type": "array",
                      "minItems": 3,
                      "maxItems": 3,
                      "items": {
                        "type": "number",
                        "minimum": -100000,
                        "maximum": 100000
                      }
                    }
                  }
                }
              }
            ]
          }
        },
        "maxFlyHeight": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        }
      }
    },
    "backdrop": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "farCards"
      ],
      "properties": {
        "farCards": {
          "type": "array",
          "maxItems": 256,
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 0
          }
        }
      }
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema06 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.6/common.schema.json",
  "title": "Hyprune 0.3 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish",
        "input.control"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *worldSchema07 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.7/world.schema.json",
  "title": "Hyprune world 0.7: compressed and optimised scene assets (KHR_texture_basisu, EXT_meshopt_compression, KHR_mesh_quantization); world.json as 0.6",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.7"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "workspaceSlot": {
            "type": "string",
            "pattern": "^[0-9]{2,4}$"
          },
          "primaryAnchor": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "toolAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "dataAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "theme": {
            "type": "string",
            "maxLength": 256
          },
          "spawnId": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "freePlacementZones": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 32
          },
          "playable": {
            "type": "object",
            "additionalProperties": false,
            "anyOf": [
              {
                "required": [
                  "volumes"
                ],
                "properties": {
                  "volumes": {}
                }
              },
              {
                "required": [
                  "maxFlyHeight"
                ],
                "properties": {
                  "maxFlyHeight": {}
                }
              }
            ],
            "properties": {
              "volumes": {
                "type": "array",
                "minItems": 1,
                "maxItems": 8,
                "items": {
                  "anyOf": [
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "box"
                        },
                        "min": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        },
                        "max": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        }
                      }
                    },
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "points"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "convex"
                        },
                        "points": {
                          "type": "array",
                          "minItems": 4,
                          "maxItems": 16,
                          "uniqueItems": true,
                          "items": {
                            "type": "array",
                            "minItems": 3,
                            "maxItems": 3,
                            "items": {
                              "type": "number",
                              "minimum": -100000,
                              "maximum": 100000
                            }
                          }
                        }
                      }
                    }
                  ]
                }
              },
              "maxFlyHeight": {
                "type": "number",
                "minimum": -100000,
                "maximum": 100000
              }
            },
            "required": [
              "volumes"
            ]
          }
        },
        "required": [
          "id",
          "workspaceSlot",
          "primaryAnchor",
          "toolAnchors",
          "dataAnchors",
          "theme"
        ],
        "additionalProperties": false
      },
      "maxItems": 32
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        },
        "outline": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "colour": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 3,
              "maxItems": 3
            },
            "widthPx": {
              "type": "number",
              "minimum": 1,
              "maximum": 4
            },
            "depthThreshold": {
              "type": "number",
              "minimum": 0.0001,
              "maximum": 1
            },
            "normalThreshold": {
              "type": "number",
              "minimum": 0.001,
              "maximum": 2
            }
          },
          "required": [
            "enabled",
            "colour",
            "widthPx",
            "depthThreshold",
            "normalThreshold"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    },
    "visibility": {
      "type": "object",
      "properties": {
        "areas": {
          "type": "array",
          "maxItems": 256,
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "bounds": {
                "type": "object",
                "properties": {
                  "min": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "max": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  }
                },
                "required": [
                  "min",
                  "max"
                ],
                "additionalProperties": false
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9999
                },
                "maxItems": 10000,
                "uniqueItems": true
              },
              "visibleFrom": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "maxItems": 256,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "bounds",
              "nodes",
              "visibleFrom"
            ],
            "additionalProperties": false
          }
        }
      },
      "required": [
        "areas"
      ],
      "additionalProperties": false
    },
    "playable": {
      "type": "object",
      "additionalProperties": false,
      "anyOf": [
        {
          "required": [
            "volumes"
          ],
          "properties": {
            "volumes": {}
          }
        },
        {
          "required": [
            "maxFlyHeight"
          ],
          "properties": {
            "maxFlyHeight": {}
          }
        }
      ],
      "properties": {
        "volumes": {
          "type": "array",
          "minItems": 1,
          "maxItems": 8,
          "items": {
            "anyOf": [
              {
                "type": "object",
                "required": [
                  "type",
                  "min",
                  "max"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "box"
                  },
                  "min": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  },
                  "max": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  }
                }
              },
              {
                "type": "object",
                "required": [
                  "type",
                  "points"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "convex"
                  },
                  "points": {
                    "type": "array",
                    "minItems": 4,
                    "maxItems": 16,
                    "uniqueItems": true,
                    "items": {
                      "type": "array",
                      "minItems": 3,
                      "maxItems": 3,
                      "items": {
                        "type": "number",
                        "minimum": -100000,
                        "maximum": 100000
                      }
                    }
                  }
                }
              }
            ]
          }
        },
        "maxFlyHeight": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        }
      }
    },
    "backdrop": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "farCards"
      ],
      "properties": {
        "farCards": {
          "type": "array",
          "maxItems": 256,
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 0
          }
        }
      }
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema07 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.7/common.schema.json",
  "title": "Hyprune 0.3 common",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish",
        "input.control"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *worldSchema08 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.8/world.schema.json",
  "title": "Hyprune world 0.8: placed world objects (glTF object assets, instances, declarative node animation); scene assets as 0.7",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.8"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "workspaceSlot": {
            "type": "string",
            "pattern": "^[0-9]{2,4}$"
          },
          "primaryAnchor": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "toolAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "dataAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "theme": {
            "type": "string",
            "maxLength": 256
          },
          "spawnId": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "freePlacementZones": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 32
          },
          "playable": {
            "type": "object",
            "additionalProperties": false,
            "anyOf": [
              {
                "required": [
                  "volumes"
                ],
                "properties": {
                  "volumes": {}
                }
              },
              {
                "required": [
                  "maxFlyHeight"
                ],
                "properties": {
                  "maxFlyHeight": {}
                }
              }
            ],
            "properties": {
              "volumes": {
                "type": "array",
                "minItems": 1,
                "maxItems": 8,
                "items": {
                  "anyOf": [
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "box"
                        },
                        "min": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        },
                        "max": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        }
                      }
                    },
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "points"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "convex"
                        },
                        "points": {
                          "type": "array",
                          "minItems": 4,
                          "maxItems": 16,
                          "uniqueItems": true,
                          "items": {
                            "type": "array",
                            "minItems": 3,
                            "maxItems": 3,
                            "items": {
                              "type": "number",
                              "minimum": -100000,
                              "maximum": 100000
                            }
                          }
                        }
                      }
                    }
                  ]
                }
              },
              "maxFlyHeight": {
                "type": "number",
                "minimum": -100000,
                "maximum": 100000
              }
            },
            "required": [
              "volumes"
            ]
          }
        },
        "required": [
          "id",
          "workspaceSlot",
          "primaryAnchor",
          "toolAnchors",
          "dataAnchors",
          "theme"
        ],
        "additionalProperties": false
      },
      "maxItems": 32
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        },
        "outline": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "colour": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 3,
              "maxItems": 3
            },
            "widthPx": {
              "type": "number",
              "minimum": 1,
              "maximum": 4
            },
            "depthThreshold": {
              "type": "number",
              "minimum": 0.0001,
              "maximum": 1
            },
            "normalThreshold": {
              "type": "number",
              "minimum": 0.001,
              "maximum": 2
            }
          },
          "required": [
            "enabled",
            "colour",
            "widthPx",
            "depthThreshold",
            "normalThreshold"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    },
    "visibility": {
      "type": "object",
      "properties": {
        "areas": {
          "type": "array",
          "maxItems": 256,
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "bounds": {
                "type": "object",
                "properties": {
                  "min": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "max": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  }
                },
                "required": [
                  "min",
                  "max"
                ],
                "additionalProperties": false
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9999
                },
                "maxItems": 10000,
                "uniqueItems": true
              },
              "visibleFrom": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "maxItems": 256,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "bounds",
              "nodes",
              "visibleFrom"
            ],
            "additionalProperties": false
          }
        }
      },
      "required": [
        "areas"
      ],
      "additionalProperties": false
    },
    "playable": {
      "type": "object",
      "additionalProperties": false,
      "anyOf": [
        {
          "required": [
            "volumes"
          ],
          "properties": {
            "volumes": {}
          }
        },
        {
          "required": [
            "maxFlyHeight"
          ],
          "properties": {
            "maxFlyHeight": {}
          }
        }
      ],
      "properties": {
        "volumes": {
          "type": "array",
          "minItems": 1,
          "maxItems": 8,
          "items": {
            "anyOf": [
              {
                "type": "object",
                "required": [
                  "type",
                  "min",
                  "max"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "box"
                  },
                  "min": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  },
                  "max": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  }
                }
              },
              {
                "type": "object",
                "required": [
                  "type",
                  "points"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "convex"
                  },
                  "points": {
                    "type": "array",
                    "minItems": 4,
                    "maxItems": 16,
                    "uniqueItems": true,
                    "items": {
                      "type": "array",
                      "minItems": 3,
                      "maxItems": 3,
                      "items": {
                        "type": "number",
                        "minimum": -100000,
                        "maximum": 100000
                      }
                    }
                  }
                }
              }
            ]
          }
        },
        "maxFlyHeight": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        }
      }
    },
    "backdrop": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "farCards"
      ],
      "properties": {
        "farCards": {
          "type": "array",
          "maxItems": 256,
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 0
          }
        }
      }
    },
    "objectAssets": {
      "description": "Reusable glTF object assets (RFC-0004 world 0.8). Render-only; optional static collision proxy and declarative node animation.",
      "type": "array",
      "maxItems": 32,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "scene": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "type": "string",
                "pattern": "\\.(gltf|glb)$"
              }
            ]
          },
          "library": {
            "description": "Reserved: a shared object library reference (id@version). Rejected by validators and core in world 0.8.",
            "type": "string",
            "minLength": 3,
            "maxLength": 256,
            "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)*@[0-9A-Za-z.+-]+$"
          },
          "animations": {
            "type": "array",
            "maxItems": 16,
            "items": {
              "type": "object",
              "properties": {
                "node": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "type": {
                  "enum": [
                    "spin",
                    "orbit",
                    "bob"
                  ]
                },
                "axis": {
                  "type": "array",
                  "items": {
                    "type": "number",
                    "minimum": -1.001,
                    "maximum": 1.001
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "pivot": {
                  "type": "array",
                  "items": {
                    "type": "number",
                    "minimum": -10000,
                    "maximum": 10000
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "amplitude": {
                  "type": "number",
                  "exclusiveMinimum": 0,
                  "maximum": 10
                },
                "period": {
                  "type": "number",
                  "minimum": 0.5,
                  "maximum": 3600
                },
                "phase": {
                  "type": "number",
                  "minimum": 0,
                  "exclusiveMaximum": 1
                }
              },
              "required": [
                "node",
                "type",
                "axis",
                "period"
              ],
              "additionalProperties": false
            }
          },
          "collision": {
            "type": "object",
            "properties": {
              "node": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              }
            },
            "required": [
              "node"
            ],
            "additionalProperties": false
          },
          "lighting": {
            "type": "object",
            "properties": {
              "mode": {
                "enum": [
                  "probe",
                  "unlit"
                ]
              }
            },
            "required": [
              "mode"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id"
        ],
        "anyOf": [
          {
            "properties": {
              "scene": {
                "type": "string"
              }
            },
            "required": [
              "scene"
            ]
          },
          {
            "properties": {
              "library": {
                "type": "string"
              }
            },
            "required": [
              "library"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    "objects": {
      "description": "Placed instances of objectAssets (RFC-0004 world 0.8).",
      "type": "array",
      "maxItems": 256,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "asset": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "scale": {
            "type": "number",
            "minimum": 0.01,
            "maximum": 100
          },
          "speed": {
            "type": "number",
            "minimum": 0,
            "maximum": 10
          },
          "phase": {
            "type": "number",
            "minimum": 0,
            "exclusiveMaximum": 1
          },
          "ambient": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": 0,
              "maximum": 16
            },
            "minItems": 3,
            "maxItems": 3
          }
        },
        "required": [
          "id",
          "asset",
          "pose"
        ],
        "additionalProperties": false
      }
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *commonSchema08 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.8/common.schema.json",
  "title": "Hyprune world 0.8 common definitions",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish",
        "input.control"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *worldSchema09 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.9/world.schema.json",
  "title": "Hyprune world 0.9: characters, paths, seats and keepClear; object assets from package directories and libraries; 0.8 objects otherwise",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.9"
    },
    "id": {
      "$ref": "common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "description": {
      "type": "string",
      "minLength": 0,
      "maxLength": 4096
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "scene": {
      "allOf": [
        {
          "$ref": "common.schema.json#/$defs/path"
        },
        {
          "pattern": "\\.(gltf|glb)$",
          "type": "string"
        }
      ]
    },
    "preview": {
      "$ref": "common.schema.json#/$defs/path"
    },
    "defaultSpawn": {
      "$ref": "common.schema.json#/$defs/slug"
    },
    "spawns": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          }
        },
        "required": [
          "id",
          "pose"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1
    },
    "zones": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "bounds": {
            "type": "object",
            "properties": {
              "min": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              },
              "max": {
                "type": "array",
                "items": {
                  "type": "number"
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "min",
              "max"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "name",
          "bounds"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "anchors": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "size": {
            "type": "array",
            "items": {
              "type": "number",
              "exclusiveMinimum": 0
            },
            "minItems": 2,
            "maxItems": 2
          },
          "screenType": {
            "$ref": "common.schema.json#/$defs/id"
          },
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "mount": {
            "$ref": "common.schema.json#/$defs/mount"
          }
        },
        "required": [
          "id",
          "pose",
          "size",
          "screenType"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "navigation": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "mesh"
        },
        "node": {
          "$ref": "common.schema.json#/$defs/index"
        },
        "agentRadius": {
          "type": "number",
          "exclusiveMinimum": 0
        },
        "agentHeight": {
          "type": "number",
          "exclusiveMinimum": 0
        }
      },
      "required": [
        "kind",
        "node",
        "agentRadius",
        "agentHeight"
      ],
      "additionalProperties": false
    },
    "collision": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "node": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "shape": {
            "enum": [
              "mesh",
              "convex"
            ]
          },
          "layer": {
            "const": "static"
          }
        },
        "required": [
          "node",
          "shape",
          "layer"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "audio": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "path": {
            "allOf": [
              {
                "$ref": "common.schema.json#/$defs/path"
              },
              {
                "pattern": "\\.(ogg|wav)$",
                "type": "string"
              }
            ]
          },
          "position": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "radius": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "gain": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "loop": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "path",
          "position",
          "radius",
          "gain",
          "loop"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "lightmaps": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "material": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texture": {
            "$ref": "common.schema.json#/$defs/index"
          },
          "texCoord": {
            "const": 1
          },
          "encoding": {
            "enum": [
              "linear",
              "srgb"
            ]
          },
          "intensity": {
            "type": "number",
            "minimum": 0
          }
        },
        "required": [
          "material",
          "texture",
          "texCoord",
          "encoding",
          "intensity"
        ],
        "additionalProperties": false
      },
      "minItems": 0
    },
    "extensions": {
      "$ref": "common.schema.json#/$defs/extensions"
    },
    "requiredExtensions": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "minItems": 0,
      "uniqueItems": true
    },
    "areas": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "workspaceSlot": {
            "type": "string",
            "pattern": "^[0-9]{2,4}$"
          },
          "primaryAnchor": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "toolAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "dataAnchors": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 128
          },
          "theme": {
            "type": "string",
            "maxLength": 256
          },
          "spawnId": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "freePlacementZones": {
            "type": "array",
            "items": {
              "$ref": "common.schema.json#/$defs/slug"
            },
            "maxItems": 32
          },
          "playable": {
            "type": "object",
            "additionalProperties": false,
            "anyOf": [
              {
                "required": [
                  "volumes"
                ],
                "properties": {
                  "volumes": {}
                }
              },
              {
                "required": [
                  "maxFlyHeight"
                ],
                "properties": {
                  "maxFlyHeight": {}
                }
              }
            ],
            "properties": {
              "volumes": {
                "type": "array",
                "minItems": 1,
                "maxItems": 8,
                "items": {
                  "anyOf": [
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "box"
                        },
                        "min": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        },
                        "max": {
                          "type": "array",
                          "minItems": 3,
                          "maxItems": 3,
                          "items": {
                            "type": "number",
                            "minimum": -100000,
                            "maximum": 100000
                          }
                        }
                      }
                    },
                    {
                      "type": "object",
                      "required": [
                        "type",
                        "points"
                      ],
                      "additionalProperties": false,
                      "properties": {
                        "type": {
                          "const": "convex"
                        },
                        "points": {
                          "type": "array",
                          "minItems": 4,
                          "maxItems": 16,
                          "uniqueItems": true,
                          "items": {
                            "type": "array",
                            "minItems": 3,
                            "maxItems": 3,
                            "items": {
                              "type": "number",
                              "minimum": -100000,
                              "maximum": 100000
                            }
                          }
                        }
                      }
                    }
                  ]
                }
              },
              "maxFlyHeight": {
                "type": "number",
                "minimum": -100000,
                "maximum": 100000
              }
            },
            "required": [
              "volumes"
            ]
          }
        },
        "required": [
          "id",
          "workspaceSlot",
          "primaryAnchor",
          "toolAnchors",
          "dataAnchors",
          "theme"
        ],
        "additionalProperties": false
      },
      "maxItems": 32
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        },
        "outline": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "colour": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 3,
              "maxItems": 3
            },
            "widthPx": {
              "type": "number",
              "minimum": 1,
              "maximum": 4
            },
            "depthThreshold": {
              "type": "number",
              "minimum": 0.0001,
              "maximum": 1
            },
            "normalThreshold": {
              "type": "number",
              "minimum": 0.001,
              "maximum": 2
            }
          },
          "required": [
            "enabled",
            "colour",
            "widthPx",
            "depthThreshold",
            "normalThreshold"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "placementZones": {
      "type": "array",
      "items": {
        "$ref": "common.schema.json#/$defs/placementZone"
      },
      "maxItems": 32
    },
    "dataObjects": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "$ref": "common.schema.json#/$defs/dataObject"
      }
    },
    "climbables": {
      "type": "array",
      "maxItems": 128,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "type": {
            "enum": [
              "ladder",
              "rope",
              "vines",
              "chain-link"
            ]
          },
          "bottom": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "top": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "facing": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "minItems": 3,
            "maxItems": 3
          },
          "width": {
            "type": "number",
            "minimum": 0.6,
            "maximum": 32
          },
          "depth": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 2
          },
          "speed": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 8
          },
          "exits": {
            "type": "object",
            "properties": {
              "top": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "bottom": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              }
            },
            "required": [
              "top",
              "bottom"
            ],
            "additionalProperties": false
          }
        },
        "required": [
          "id",
          "type",
          "bottom",
          "top",
          "facing",
          "width",
          "depth",
          "exits"
        ],
        "additionalProperties": false
      }
    },
    "visibility": {
      "type": "object",
      "properties": {
        "areas": {
          "type": "array",
          "maxItems": 256,
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "bounds": {
                "type": "object",
                "properties": {
                  "min": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "max": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    },
                    "minItems": 3,
                    "maxItems": 3
                  }
                },
                "required": [
                  "min",
                  "max"
                ],
                "additionalProperties": false
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9999
                },
                "maxItems": 10000,
                "uniqueItems": true
              },
              "visibleFrom": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "maxItems": 256,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "bounds",
              "nodes",
              "visibleFrom"
            ],
            "additionalProperties": false
          }
        }
      },
      "required": [
        "areas"
      ],
      "additionalProperties": false
    },
    "playable": {
      "type": "object",
      "additionalProperties": false,
      "anyOf": [
        {
          "required": [
            "volumes"
          ],
          "properties": {
            "volumes": {}
          }
        },
        {
          "required": [
            "maxFlyHeight"
          ],
          "properties": {
            "maxFlyHeight": {}
          }
        }
      ],
      "properties": {
        "volumes": {
          "type": "array",
          "minItems": 1,
          "maxItems": 8,
          "items": {
            "anyOf": [
              {
                "type": "object",
                "required": [
                  "type",
                  "min",
                  "max"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "box"
                  },
                  "min": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  },
                  "max": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number",
                      "minimum": -100000,
                      "maximum": 100000
                    }
                  }
                }
              },
              {
                "type": "object",
                "required": [
                  "type",
                  "points"
                ],
                "additionalProperties": false,
                "properties": {
                  "type": {
                    "const": "convex"
                  },
                  "points": {
                    "type": "array",
                    "minItems": 4,
                    "maxItems": 16,
                    "uniqueItems": true,
                    "items": {
                      "type": "array",
                      "minItems": 3,
                      "maxItems": 3,
                      "items": {
                        "type": "number",
                        "minimum": -100000,
                        "maximum": 100000
                      }
                    }
                  }
                }
              }
            ]
          }
        },
        "maxFlyHeight": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        }
      }
    },
    "backdrop": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "farCards"
      ],
      "properties": {
        "farCards": {
          "type": "array",
          "maxItems": 256,
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 0
          }
        }
      }
    },
    "objectAssets": {
      "description": "Object assets (RFC-0004 world 0.8, RFC-0011 world 0.9): exactly one source each, an inline package glTF, a package object directory or a library object.",
      "type": "array",
      "maxItems": 32,
      "items": {
        "oneOf": [
          {
            "$ref": "#/$defs/inlineObjectAsset"
          },
          {
            "$ref": "#/$defs/dirObjectAsset"
          },
          {
            "$ref": "#/$defs/libraryObjectAsset"
          }
        ]
      }
    },
    "objects": {
      "description": "Placed instances of objectAssets (RFC-0004 world 0.8, RFC-0011 world 0.9). pose is required unless path is set and forbidden with it; at most 16 character instances.",
      "type": "array",
      "maxItems": 256,
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "asset": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "pose": {
            "$ref": "common.schema.json#/$defs/pose"
          },
          "scale": {
            "type": "number",
            "minimum": 0.01,
            "maximum": 100
          },
          "speed": {
            "type": "number",
            "minimum": 0,
            "maximum": 10
          },
          "phase": {
            "type": "number",
            "minimum": 0,
            "exclusiveMaximum": 1
          },
          "ambient": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": 0,
              "maximum": 16
            },
            "minItems": 3,
            "maxItems": 3
          },
          "path": {
            "description": "A paths[] slug with use route (characters only); pose is omitted.",
            "$ref": "common.schema.json#/$defs/slug"
          },
          "reverse": {
            "type": "boolean",
            "description": "Only with path: start walking towards decreasing arc length."
          },
          "seat": {
            "description": "Characters without path: sits there permanently.",
            "$ref": "#/$defs/seatRef"
          },
          "accent": {
            "type": "array",
            "items": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "minItems": 3,
            "maxItems": 3,
            "description": "Characters only: linear RGB for the accent material."
          },
          "interactions": {
            "type": "array",
            "items": {
              "$ref": "#/$defs/instanceInteraction"
            },
            "minItems": 1,
            "maxItems": 8,
            "description": "Characters only; replaces the asset's list. Ids unique per list."
          },
          "locked": {
            "type": "boolean",
            "description": "Movable assets only."
          }
        },
        "required": [
          "id",
          "asset"
        ],
        "additionalProperties": false
      }
    },
    "paths": {
      "description": "Authored paths (RFC-0011 Paths): at most 64, 1,024 points in total.",
      "type": "array",
      "items": {
        "$ref": "#/$defs/path"
      },
      "maxItems": 64
    },
    "seats": {
      "description": "World seat anchors (RFC-0011 Seats and sitting).",
      "type": "array",
      "items": {
        "$ref": "#/$defs/worldSeat"
      },
      "maxItems": 64
    },
    "keepClear": {
      "description": "Boxes that objects may not block and route stops avoid.",
      "type": "array",
      "items": {
        "$ref": "#/$defs/keepClearBox"
      },
      "maxItems": 64
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "scene",
    "defaultSpawn",
    "spawns",
    "attribution"
  ],
  "additionalProperties": false,
  "$defs": {
    "animation": {
      "type": "object",
      "properties": {
        "node": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "type": {
          "enum": [
            "spin",
            "orbit",
            "bob"
          ]
        },
        "axis": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 1.001,
            "minimum": -1.001
          },
          "minItems": 3,
          "maxItems": 3
        },
        "pivot": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 10000,
            "minimum": -10000
          },
          "minItems": 3,
          "maxItems": 3
        },
        "amplitude": {
          "type": "number",
          "maximum": 10,
          "exclusiveMinimum": 0
        },
        "period": {
          "type": "number",
          "maximum": 3600,
          "minimum": 0.5
        },
        "phase": {
          "type": "number",
          "minimum": 0,
          "exclusiveMaximum": 1
        }
      },
      "required": [
        "node",
        "type",
        "axis",
        "period"
      ],
      "additionalProperties": false,
      "description": "A world 0.8 declarative node animation (spin, orbit or bob)."
    },
    "slotPose": {
      "$ref": "common.schema.json#/$defs/pose"
    },
    "interaction": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "label": {
          "type": "string",
          "minLength": 1,
          "maxLength": 32,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "prompt": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "action": {
          "oneOf": [
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "say"
                },
                "lines": {
                  "type": "array",
                  "items": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 160,
                    "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
                  },
                  "minItems": 1,
                  "maxItems": 8
                }
              },
              "required": [
                "type",
                "lines"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "panel"
                },
                "panel": {
                  "$ref": "common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "type",
                "panel"
              ],
              "additionalProperties": false
            }
          ]
        }
      },
      "required": [
        "id",
        "label",
        "action"
      ],
      "additionalProperties": false,
      "description": "Asset-level character interaction: only say and panel, which reference nothing in a world."
    },
    "seatAnchor": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "position": {
          "$ref": "common.schema.json#/$defs/vec3"
        },
        "yaw": {
          "type": "number",
          "maximum": 360,
          "minimum": -360
        },
        "height": {
          "type": "number",
          "maximum": 1,
          "minimum": 0.2
        },
        "use": {
          "type": "array",
          "items": {
            "enum": [
              "sit"
            ]
          },
          "minItems": 1,
          "maxItems": 1,
          "uniqueItems": true
        },
        "approach": {
          "type": "number",
          "maximum": 2,
          "minimum": 0.3
        }
      },
      "required": [
        "id",
        "position",
        "yaw",
        "height"
      ],
      "additionalProperties": false,
      "description": "Where the sitter's root stands at the start of sit (asset space), facing yaw degrees about +Y from +Z. perch, lean and ground uses are reserved."
    },
    "objectTraits": {
      "type": "object",
      "properties": {
        "decorative": {
          "type": "object",
          "properties": {
            "animations": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/animation"
              },
              "minItems": 1,
              "maxItems": 16
            }
          },
          "required": [
            "animations"
          ],
          "additionalProperties": false
        },
        "collidable": {
          "type": "object",
          "properties": {
            "node": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            }
          },
          "required": [
            "node"
          ],
          "additionalProperties": false,
          "description": "The 0.8 static collision proxy: a mesh node, not animated, not in an animated subtree."
        },
        "movable": {
          "type": "object",
          "properties": {
            "surfaces": {
              "type": "array",
              "items": {
                "enum": [
                  "floor",
                  "wall",
                  "ceiling",
                  "any"
                ]
              },
              "minItems": 1,
              "maxItems": 4,
              "uniqueItems": true
            },
            "upright": {
              "type": "boolean"
            },
            "scale": {
              "type": "array",
              "items": {
                "type": "number",
                "maximum": 4,
                "minimum": 0.25
              },
              "minItems": 2,
              "maxItems": 2
            }
          },
          "additionalProperties": false,
          "description": "Offered for user placement. Defaults: surfaces [\"floor\"], upright true for floor and ceiling, scale [1, 1]."
        },
        "character": {
          "type": "object",
          "properties": {
            "clips": {
              "type": "object",
              "propertyNames": {
                "type": "string",
                "minLength": 1,
                "maxLength": 64,
                "pattern": "^[a-z][a-z0-9_]*$"
              },
              "properties": {
                "walk": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                },
                "idle": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                }
              },
              "additionalProperties": {
                "type": "object",
                "properties": {
                  "speed": {
                    "type": "number",
                    "maximum": 15,
                    "exclusiveMinimum": 0.05
                  },
                  "seatHeight": {
                    "type": "number",
                    "maximum": 1,
                    "minimum": 0.2
                  }
                },
                "additionalProperties": false,
                "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
              },
              "required": [
                "walk",
                "idle"
              ],
              "maxProperties": 64
            },
            "height": {
              "type": "number",
              "maximum": 2.2,
              "minimum": 1.2
            },
            "radius": {
              "type": "number",
              "maximum": 0.5,
              "minimum": 0.25
            },
            "accent": {
              "type": "boolean"
            },
            "interactions": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/interaction"
              },
              "minItems": 1,
              "maxItems": 8
            }
          },
          "required": [
            "clips",
            "height"
          ],
          "additionalProperties": false,
          "description": "RFC-0010 character: clips with measured values (walk and idle required), height and radius in metres."
        },
        "seat": {
          "type": "object",
          "properties": {
            "anchors": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/seatAnchor"
              },
              "minItems": 1,
              "maxItems": 8
            },
            "capacity": {
              "type": "integer",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "anchors"
          ],
          "additionalProperties": false
        },
        "mount": {
          "description": "Reserved (RFC-0011 P4): a prop that holds a window. Rejected in world format 0.9.",
          "not": {}
        },
        "machine": {
          "description": "Reserved for RFC-0013 (live objects). Rejected in world format 0.9.",
          "not": {}
        },
        "interactive": {
          "description": "Reserved (RFC-0011 P4): use actions on ordinary objects. Rejected in world format 0.9.",
          "not": {}
        },
        "mover": {
          "description": "World format 0.10 (RFC-0011 P3). Rejected in world format 0.9.",
          "not": {}
        }
      },
      "additionalProperties": false,
      "description": "Inline object asset traits (object.json 0.9 shapes). Unknown or reserved names reject the world."
    },
    "objectSlots": {
      "type": "object",
      "properties": {
        "states": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "common.schema.json#/$defs/slug"
              },
              "materials": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "palette": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "id": {
                      "$ref": "common.schema.json#/$defs/slug"
                    },
                    "colour": {
                      "type": "array",
                      "items": {
                        "type": "number",
                        "maximum": 1,
                        "minimum": 0
                      },
                      "minItems": 3,
                      "maxItems": 3,
                      "description": "Linear RGB, 0-1."
                    },
                    "emission": {
                      "type": "number",
                      "maximum": 16,
                      "minimum": 0
                    }
                  },
                  "required": [
                    "id",
                    "colour",
                    "emission"
                  ],
                  "additionalProperties": false
                },
                "minItems": 1,
                "maxItems": 16
              }
            },
            "required": [
              "id",
              "palette"
            ],
            "additionalProperties": false,
            "oneOf": [
              {
                "properties": {
                  "materials": {
                    "type": "array"
                  }
                },
                "required": [
                  "materials"
                ]
              },
              {
                "properties": {
                  "nodes": {
                    "type": "array"
                  }
                },
                "required": [
                  "nodes"
                ]
              }
            ]
          },
          "minItems": 1,
          "maxItems": 8
        },
        "labels": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "size": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 16,
                  "exclusiveMinimum": 0
                },
                "minItems": 2,
                "maxItems": 2
              },
              "maxChars": {
                "type": "integer",
                "minimum": 1,
                "maximum": 64
              }
            },
            "required": [
              "id",
              "pose",
              "size",
              "maxChars"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "spawns": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "effects": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        }
      },
      "additionalProperties": false,
      "description": "Inline object asset slots (object.json 0.9 shapes): authored parts that RFC-0013 drives. Validated and inert in 0.9."
    },
    "instanceInteraction": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "label": {
          "type": "string",
          "minLength": 1,
          "maxLength": 32,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "prompt": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "action": {
          "oneOf": [
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "say"
                },
                "lines": {
                  "type": "array",
                  "items": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 160,
                    "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
                  },
                  "minItems": 1,
                  "maxItems": 8
                }
              },
              "required": [
                "type",
                "lines"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "waypoint"
                },
                "target": {
                  "oneOf": [
                    {
                      "type": "object",
                      "properties": {
                        "zoneId": {
                          "$ref": "common.schema.json#/$defs/slug"
                        }
                      },
                      "required": [
                        "zoneId"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "object",
                      "properties": {
                        "position": {
                          "$ref": "common.schema.json#/$defs/vec3"
                        }
                      },
                      "required": [
                        "position"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "object",
                      "properties": {
                        "anchor": {
                          "$ref": "common.schema.json#/$defs/slug"
                        }
                      },
                      "required": [
                        "anchor"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "object",
                      "properties": {
                        "object": {
                          "$ref": "common.schema.json#/$defs/slug"
                        }
                      },
                      "required": [
                        "object"
                      ],
                      "additionalProperties": false
                    }
                  ]
                }
              },
              "required": [
                "type",
                "target"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "guide"
                },
                "path": {
                  "$ref": "common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "type",
                "path"
              ],
              "additionalProperties": false,
              "description": "Walk the player along a route path (the slug of a paths[] entry with use route)."
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "panel"
                },
                "panel": {
                  "$ref": "common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "type",
                "panel"
              ],
              "additionalProperties": false
            }
          ]
        }
      },
      "required": [
        "id",
        "label",
        "action"
      ],
      "additionalProperties": false,
      "description": "Instance-level character interaction: say, waypoint (zone, position, anchor or object), guide (a route path) or panel. travel and quest are reserved and rejected in world 0.9."
    },
    "seatRef": {
      "type": "string",
      "minLength": 1,
      "maxLength": 129,
      "pattern": "^[a-z][a-z0-9-]*(/[a-z][a-z0-9-]*)?$",
      "description": "A world seats[] id, <objectId>/<anchorId> or <objectId> (any free anchor of a seat object)."
    },
    "pathPoint": {
      "type": "object",
      "properties": {
        "position": {
          "$ref": "common.schema.json#/$defs/vec3"
        },
        "type": {
          "enum": [
            "line",
            "curve",
            "arc"
          ]
        }
      },
      "required": [
        "position",
        "type"
      ],
      "additionalProperties": false,
      "description": "The segment type arriving at this point; an open path ignores its first point's type, a closed path uses it for the closing segment."
    },
    "pathStop": {
      "type": "object",
      "properties": {
        "point": {
          "type": "integer",
          "minimum": 0,
          "maximum": 255
        },
        "seconds": {
          "type": "number",
          "minimum": 0.5,
          "maximum": 600
        },
        "clip": {
          "enum": [
            "look_around",
            "wave",
            "nod",
            "idle_fidget"
          ]
        },
        "face": {
          "oneOf": [
            {
              "type": "object",
              "properties": {
                "position": {
                  "$ref": "common.schema.json#/$defs/vec3"
                }
              },
              "required": [
                "position"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "anchor": {
                  "$ref": "common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "anchor"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "object": {
                  "$ref": "common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "object"
              ],
              "additionalProperties": false
            }
          ]
        },
        "seat": {
          "$ref": "#/$defs/seatRef"
        }
      },
      "required": [
        "point"
      ],
      "additionalProperties": false,
      "not": {
        "anyOf": [
          {
            "properties": {
              "clip": {
                "type": "string"
              },
              "face": {
                "type": "object"
              }
            },
            "required": [
              "clip",
              "face"
            ]
          },
          {
            "properties": {
              "clip": {
                "type": "string"
              },
              "seat": {
                "type": "string"
              }
            },
            "required": [
              "clip",
              "seat"
            ]
          },
          {
            "properties": {
              "face": {
                "type": "object"
              },
              "seat": {
                "type": "string"
              }
            },
            "required": [
              "face",
              "seat"
            ]
          }
        ]
      },
      "description": "A stop at a point index (one per point, below the point count): seconds (default 3) and at most one of clip, face or seat. Track stops take only point and seconds; generic paths have no stops."
    },
    "path": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "name": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "use": {
          "enum": [
            "route",
            "track",
            "generic"
          ]
        },
        "closed": {
          "type": "boolean"
        },
        "points": {
          "type": "array",
          "items": {
            "$ref": "#/$defs/pathPoint"
          },
          "minItems": 2,
          "maxItems": 256
        },
        "speed": {
          "type": "number",
          "minimum": 0.1,
          "maximum": 30
        },
        "stops": {
          "type": "array",
          "items": {
            "$ref": "#/$defs/pathStop"
          },
          "minItems": 0,
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "points"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "use": {
                "const": "track"
              }
            },
            "required": [
              "use"
            ]
          },
          "then": {
            "properties": {
              "stops": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "clip": false,
                    "face": false,
                    "seat": false
                  }
                }
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "use": {
                "enum": [
                  "route",
                  "track"
                ]
              }
            },
            "required": [
              "use"
            ]
          },
          "else": {
            "properties": {
              "stops": {
                "type": "array",
                "maxItems": 0
              }
            }
          }
        }
      ],
      "description": "An authored path (RFC-0011 Paths). use defaults to generic, closed to false."
    },
    "worldSeat": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "height": {
          "type": "number",
          "minimum": 0.2,
          "maximum": 1
        },
        "use": {
          "type": "array",
          "items": {
            "enum": [
              "sit"
            ]
          },
          "minItems": 1,
          "maxItems": 1,
          "uniqueItems": true
        },
        "approach": {
          "type": "number",
          "minimum": 0.3,
          "maximum": 2
        }
      },
      "required": [
        "id",
        "pose",
        "height"
      ],
      "additionalProperties": false,
      "description": "A world-space seat anchor: pose is where the sitter's root stands at the start of sit, facing the pose's +Z; height is the seat surface above it; approach (default 0.6 m) the free floor in front."
    },
    "keepClearBox": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "min": {
          "$ref": "common.schema.json#/$defs/vec3"
        },
        "max": {
          "$ref": "common.schema.json#/$defs/vec3"
        }
      },
      "required": [
        "id",
        "min",
        "max"
      ],
      "additionalProperties": false,
      "description": "An axis-aligned box objects may not block (doorways, stairs, passages); min below max on every axis."
    },
    "inlineObjectAsset": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "scene": {
          "allOf": [
            {
              "$ref": "common.schema.json#/$defs/path"
            },
            {
              "type": "string",
              "pattern": "\\.(gltf|glb)$"
            }
          ]
        },
        "animations": {
          "type": "array",
          "maxItems": 16,
          "items": {
            "type": "object",
            "properties": {
              "node": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "type": {
                "enum": [
                  "spin",
                  "orbit",
                  "bob"
                ]
              },
              "axis": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -1.001,
                  "maximum": 1.001
                },
                "minItems": 3,
                "maxItems": 3
              },
              "pivot": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 3
              },
              "amplitude": {
                "type": "number",
                "exclusiveMinimum": 0,
                "maximum": 10
              },
              "period": {
                "type": "number",
                "minimum": 0.5,
                "maximum": 3600
              },
              "phase": {
                "type": "number",
                "minimum": 0,
                "exclusiveMaximum": 1
              }
            },
            "required": [
              "node",
              "type",
              "axis",
              "period"
            ],
            "additionalProperties": false
          }
        },
        "collision": {
          "type": "object",
          "properties": {
            "node": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            }
          },
          "required": [
            "node"
          ],
          "additionalProperties": false
        },
        "lighting": {
          "type": "object",
          "properties": {
            "mode": {
              "enum": [
                "probe",
                "unlit"
              ]
            }
          },
          "required": [
            "mode"
          ],
          "additionalProperties": false
        },
        "traits": {
          "$ref": "#/$defs/objectTraits"
        },
        "slots": {
          "$ref": "#/$defs/objectSlots"
        }
      },
      "required": [
        "id",
        "scene"
      ],
      "additionalProperties": false,
      "description": "A package glTF asset: animations is shorthand for traits.decorative.animations and collision for traits.collidable (giving both forms of one is invalid)."
    },
    "dirObjectAsset": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "dir": {
          "$ref": "common.schema.json#/$defs/path"
        }
      },
      "required": [
        "id",
        "dir"
      ],
      "additionalProperties": false,
      "description": "A package object directory (no trailing slash) holding object.json (object format at most the world's) and its scene."
    },
    "libraryObjectAsset": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "library": {
          "description": "An installed shared object library, <reverse-domain id>@<semver>, resolved exactly.",
          "type": "string",
          "minLength": 3,
          "maxLength": 256,
          "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)*@[0-9A-Za-z.+-]+$"
        },
        "object": {
          "$ref": "common.schema.json#/$defs/slug"
        }
      },
      "required": [
        "id",
        "library",
        "object"
      ],
      "additionalProperties": false,
      "description": "An object of an installed library; a missing library fails activation."
    }
  }
})schema";
inline constexpr const char *commonSchema09 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.9/common.schema.json",
  "title": "Hyprune world 0.9 common definitions",
  "$defs": {
    "id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+$"
    },
    "slug": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9-]*$"
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$"
    },
    "path": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "pattern": "^(?!.*(?:^|/)\\.{1,2}(?:/|$))[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"
    },
    "pose": {
      "type": "object",
      "properties": {
        "position": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 3,
          "maxItems": 3
        },
        "rotation": {
          "type": "array",
          "items": {
            "type": "number"
          },
          "minItems": 4,
          "maxItems": 4
        }
      },
      "required": [
        "position",
        "rotation"
      ],
      "additionalProperties": false
    },
    "index": {
      "type": "integer",
      "minimum": 0
    },
    "capability": {
      "enum": [
        "state.read",
        "world.control",
        "surface.control",
        "movement.control",
        "shell.control",
        "runtime.control",
        "data.publish",
        "input.control"
      ]
    },
    "extensions": {
      "type": "object",
      "propertyNames": {
        "$ref": "common.schema.json#/$defs/id"
      },
      "additionalProperties": {
        "type": "object"
      }
    },
    "revision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "mount": {
      "type": "object",
      "properties": {
        "type": {
          "enum": [
            "facade-wall",
            "hanging-wall",
            "wall-inset-console",
            "banner",
            "lectern",
            "operator-desk",
            "field-rig",
            "kiosk-pillar",
            "globe",
            "crate",
            "blast-door-screen",
            "terminal-bench",
            "radar-dish"
          ]
        },
        "role": {
          "enum": [
            "mounted-workspace",
            "tool-mount",
            "data-object"
          ]
        },
        "frameNode": {
          "type": "integer",
          "minimum": 0
        },
        "limits": {
          "type": "object",
          "properties": {
            "minAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "maxAspect": {
              "type": "number",
              "minimum": 0.05,
              "maximum": 20
            },
            "minSize": {
              "$ref": "common.schema.json#/$defs/size2"
            },
            "maxSize": {
              "$ref": "common.schema.json#/$defs/size2"
            }
          },
          "required": [
            "minAspect",
            "maxAspect",
            "minSize",
            "maxSize"
          ],
          "additionalProperties": false
        },
        "interaction": {
          "enum": [
            "focus-in",
            "in-place",
            "none"
          ]
        }
      },
      "required": [
        "type",
        "role",
        "interaction"
      ],
      "additionalProperties": false
    },
    "area": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "workspaceSlot": {
          "type": "string",
          "pattern": "^[0-9]{2,4}$"
        },
        "primaryAnchor": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "toolAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "dataAnchors": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 128
        },
        "theme": {
          "type": "string",
          "maxLength": 256
        },
        "spawnId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "freePlacementZones": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/slug"
          },
          "maxItems": 32
        }
      },
      "required": [
        "id",
        "workspaceSlot",
        "primaryAnchor",
        "toolAnchors",
        "dataAnchors",
        "theme"
      ],
      "additionalProperties": false
    },
    "atmosphere": {
      "type": "object",
      "properties": {
        "fog": {
          "type": "object",
          "properties": {
            "color": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "density": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            },
            "heightFalloff": {
              "type": "number",
              "minimum": 0,
              "maximum": 10
            },
            "baseHeight": {
              "type": "number",
              "minimum": -10000,
              "maximum": 10000
            },
            "excludeUnlit": {
              "type": "boolean"
            }
          },
          "required": [
            "color",
            "density",
            "heightFalloff",
            "baseHeight",
            "excludeUnlit"
          ],
          "additionalProperties": false
        },
        "sky": {
          "type": "object",
          "properties": {
            "zenith": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "horizon": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "ground": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "sun": {
              "type": "object",
              "properties": {
                "direction": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "color": {
                  "$ref": "common.schema.json#/$defs/vec3"
                },
                "intensity": {
                  "type": "number",
                  "minimum": 0,
                  "maximum": 10000
                },
                "angularRadius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1
                }
              },
              "required": [
                "direction",
                "color",
                "intensity",
                "angularRadius"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "zenith",
            "horizon",
            "ground",
            "sun"
          ],
          "additionalProperties": false
        },
        "environment": {
          "type": "object",
          "properties": {
            "faces": {
              "type": "array",
              "items": {
                "$ref": "common.schema.json#/$defs/path"
              },
              "minItems": 6,
              "maxItems": 6
            },
            "encoding": {
              "enum": [
                "srgb",
                "linear"
              ]
            },
            "intensity": {
              "type": "number",
              "minimum": 0,
              "maximum": 10000
            },
            "rotation": {
              "type": "number",
              "minimum": -6.283186,
              "maximum": 6.283186
            },
            "maxResolution": {
              "type": "integer",
              "minimum": 16,
              "maximum": 512
            }
          },
          "required": [
            "faces",
            "encoding",
            "intensity",
            "rotation",
            "maxResolution"
          ],
          "additionalProperties": false
        },
        "exposure": {
          "type": "number",
          "minimum": 0.01,
          "maximum": 100
        },
        "toneMapping": {
          "enum": [
            "aces",
            "reinhard",
            "linear"
          ]
        },
        "bloom": {
          "type": "object",
          "properties": {
            "strength": {
              "type": "number",
              "minimum": 0,
              "maximum": 2
            },
            "threshold": {
              "type": "number",
              "minimum": 0,
              "maximum": 100
            },
            "radius": {
              "type": "number",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "strength",
            "threshold",
            "radius"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "fog",
        "sky",
        "exposure",
        "toneMapping",
        "bloom"
      ],
      "additionalProperties": false
    },
    "vec3": {
      "type": "array",
      "items": {
        "type": "number"
      },
      "minItems": 3,
      "maxItems": 3
    },
    "size2": {
      "type": "array",
      "items": {
        "type": "number",
        "exclusiveMinimum": 0
      },
      "minItems": 2,
      "maxItems": 2
    },
    "placementZone": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "bounds": {
          "type": "object",
          "properties": {
            "min": {
              "$ref": "common.schema.json#/$defs/vec3"
            },
            "max": {
              "$ref": "common.schema.json#/$defs/vec3"
            }
          },
          "required": [
            "min",
            "max"
          ],
          "additionalProperties": false
        },
        "windowSizes": {
          "type": "array",
          "items": {
            "$ref": "common.schema.json#/$defs/size2"
          },
          "maxItems": 8,
          "minItems": 1
        },
        "clearance": {
          "type": "number",
          "minimum": 0.04,
          "maximum": 1
        }
      },
      "required": [
        "id",
        "bounds",
        "windowSizes",
        "clearance"
      ],
      "additionalProperties": false
    },
    "dataObject": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "areaId": {
          "$ref": "common.schema.json#/$defs/slug"
        },
        "pose": {
          "$ref": "common.schema.json#/$defs/pose"
        },
        "mount": {
          "$ref": "common.schema.json#/$defs/mount"
        },
        "node": {
          "type": "integer",
          "minimum": 0
        }
      },
      "required": [
        "id",
        "areaId",
        "pose",
        "mount"
      ],
      "additionalProperties": false
    }
  }
})schema";
inline constexpr const char *objectSchema09 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.9/object.schema.json",
  "title": "Hyprune object manifest 0.9 (object.json, RFC-0011): an object asset directory with traits and RFC-0013 slots (validated, inert)",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.9"
    },
    "id": {
      "$ref": "../v0.8/common.schema.json#/$defs/slug"
    },
    "version": {
      "$ref": "../v0.8/common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "description": {
      "type": "string",
      "maxLength": 512
    },
    "category": {
      "enum": [
        "furniture",
        "lighting",
        "plants",
        "signage",
        "machines",
        "decor",
        "characters",
        "vehicles",
        "other"
      ]
    },
    "tags": {
      "type": "array",
      "items": {
        "$ref": "../v0.8/common.schema.json#/$defs/slug"
      },
      "minItems": 0,
      "maxItems": 8,
      "uniqueItems": true
    },
    "scene": {
      "allOf": [
        {
          "$ref": "../v0.8/common.schema.json#/$defs/path"
        },
        {
          "type": "string",
          "pattern": "\\.(gltf|glb)$"
        }
      ]
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "../v0.8/common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1,
      "maxItems": 64
    },
    "bounds": {
      "type": "object",
      "properties": {
        "min": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        },
        "max": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        }
      },
      "required": [
        "min",
        "max"
      ],
      "additionalProperties": false,
      "description": "Rest-pose AABB in asset space (metres, +Y up, front +Z)."
    },
    "thumbnail": {
      "allOf": [
        {
          "$ref": "../v0.8/common.schema.json#/$defs/path"
        },
        {
          "type": "string",
          "pattern": "\\.png$"
        }
      ],
      "description": "PNG, 256 x 256, at most 256 KiB."
    },
    "lighting": {
      "type": "object",
      "properties": {
        "mode": {
          "enum": [
            "probe",
            "unlit"
          ]
        }
      },
      "required": [
        "mode"
      ],
      "additionalProperties": false
    },
    "traits": {
      "type": "object",
      "properties": {
        "decorative": {
          "type": "object",
          "properties": {
            "animations": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/animation"
              },
              "minItems": 1,
              "maxItems": 16
            }
          },
          "required": [
            "animations"
          ],
          "additionalProperties": false
        },
        "collidable": {
          "type": "object",
          "properties": {
            "node": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            }
          },
          "required": [
            "node"
          ],
          "additionalProperties": false,
          "description": "The 0.8 static collision proxy: a mesh node, not animated, not in an animated subtree."
        },
        "movable": {
          "type": "object",
          "properties": {
            "surfaces": {
              "type": "array",
              "items": {
                "enum": [
                  "floor",
                  "wall",
                  "ceiling",
                  "any"
                ]
              },
              "minItems": 1,
              "maxItems": 4,
              "uniqueItems": true
            },
            "upright": {
              "type": "boolean"
            },
            "scale": {
              "type": "array",
              "items": {
                "type": "number",
                "maximum": 4,
                "minimum": 0.25
              },
              "minItems": 2,
              "maxItems": 2
            }
          },
          "additionalProperties": false,
          "description": "Offered for user placement. Defaults: surfaces [\"floor\"], upright true for floor and ceiling, scale [1, 1]."
        },
        "character": {
          "type": "object",
          "properties": {
            "clips": {
              "type": "object",
              "propertyNames": {
                "type": "string",
                "minLength": 1,
                "maxLength": 64,
                "pattern": "^[a-z][a-z0-9_]*$"
              },
              "properties": {
                "walk": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                },
                "idle": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                }
              },
              "additionalProperties": {
                "type": "object",
                "properties": {
                  "speed": {
                    "type": "number",
                    "maximum": 15,
                    "exclusiveMinimum": 0.05
                  },
                  "seatHeight": {
                    "type": "number",
                    "maximum": 1,
                    "minimum": 0.2
                  }
                },
                "additionalProperties": false,
                "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
              },
              "required": [
                "walk",
                "idle"
              ],
              "maxProperties": 64
            },
            "height": {
              "type": "number",
              "maximum": 2.2,
              "minimum": 1.2
            },
            "radius": {
              "type": "number",
              "maximum": 0.5,
              "minimum": 0.25
            },
            "accent": {
              "type": "boolean"
            },
            "interactions": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/interaction"
              },
              "minItems": 1,
              "maxItems": 8
            }
          },
          "required": [
            "clips",
            "height"
          ],
          "additionalProperties": false,
          "description": "RFC-0010 character: clips with measured values (walk and idle required), height and radius in metres."
        },
        "seat": {
          "type": "object",
          "properties": {
            "anchors": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/seatAnchor"
              },
              "minItems": 1,
              "maxItems": 8
            },
            "capacity": {
              "type": "integer",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "anchors"
          ],
          "additionalProperties": false
        },
        "mount": {
          "description": "Reserved (RFC-0011 P4): a prop that holds a window. Rejected in object format 0.9.",
          "not": {}
        },
        "machine": {
          "description": "Reserved for RFC-0013 (live objects). Rejected in object format 0.9.",
          "not": {}
        },
        "interactive": {
          "description": "Reserved (RFC-0011 P4): use actions on ordinary objects. Rejected in object format 0.9.",
          "not": {}
        },
        "mover": {
          "description": "Object format 0.10 (RFC-0011 P3). Rejected in object format 0.9.",
          "not": {}
        }
      },
      "additionalProperties": false,
      "description": "Trait name to trait data. Unknown or reserved names reject the object."
    },
    "slots": {
      "type": "object",
      "properties": {
        "states": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "materials": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "palette": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "id": {
                      "$ref": "../v0.8/common.schema.json#/$defs/slug"
                    },
                    "colour": {
                      "type": "array",
                      "items": {
                        "type": "number",
                        "maximum": 1,
                        "minimum": 0
                      },
                      "minItems": 3,
                      "maxItems": 3,
                      "description": "Linear RGB, 0-1."
                    },
                    "emission": {
                      "type": "number",
                      "maximum": 16,
                      "minimum": 0
                    }
                  },
                  "required": [
                    "id",
                    "colour",
                    "emission"
                  ],
                  "additionalProperties": false
                },
                "minItems": 1,
                "maxItems": 16
              }
            },
            "required": [
              "id",
              "palette"
            ],
            "additionalProperties": false,
            "oneOf": [
              {
                "properties": {
                  "materials": {
                    "type": "array"
                  }
                },
                "required": [
                  "materials"
                ]
              },
              {
                "properties": {
                  "nodes": {
                    "type": "array"
                  }
                },
                "required": [
                  "nodes"
                ]
              }
            ]
          },
          "minItems": 1,
          "maxItems": 8
        },
        "labels": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "size": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 16,
                  "exclusiveMinimum": 0
                },
                "minItems": 2,
                "maxItems": 2
              },
              "maxChars": {
                "type": "integer",
                "minimum": 1,
                "maximum": 64
              }
            },
            "required": [
              "id",
              "pose",
              "size",
              "maxChars"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "spawns": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "effects": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        }
      },
      "additionalProperties": false,
      "description": "Authored parts that RFC-0013 drives. Validated and inert in 0.9."
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "category",
    "scene",
    "license",
    "attribution",
    "bounds",
    "traits"
  ],
  "additionalProperties": false,
  "$defs": {
    "animation": {
      "type": "object",
      "properties": {
        "node": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "type": {
          "enum": [
            "spin",
            "orbit",
            "bob"
          ]
        },
        "axis": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 1.001,
            "minimum": -1.001
          },
          "minItems": 3,
          "maxItems": 3
        },
        "pivot": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 10000,
            "minimum": -10000
          },
          "minItems": 3,
          "maxItems": 3
        },
        "amplitude": {
          "type": "number",
          "maximum": 10,
          "exclusiveMinimum": 0
        },
        "period": {
          "type": "number",
          "maximum": 3600,
          "minimum": 0.5
        },
        "phase": {
          "type": "number",
          "minimum": 0,
          "exclusiveMaximum": 1
        }
      },
      "required": [
        "node",
        "type",
        "axis",
        "period"
      ],
      "additionalProperties": false,
      "description": "A world 0.8 declarative node animation (spin, orbit or bob)."
    },
    "slotPose": {
      "$ref": "../v0.8/common.schema.json#/$defs/pose"
    },
    "interaction": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "../v0.8/common.schema.json#/$defs/slug"
        },
        "label": {
          "type": "string",
          "minLength": 1,
          "maxLength": 32,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "prompt": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "action": {
          "oneOf": [
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "say"
                },
                "lines": {
                  "type": "array",
                  "items": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 160,
                    "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
                  },
                  "minItems": 1,
                  "maxItems": 8
                }
              },
              "required": [
                "type",
                "lines"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "panel"
                },
                "panel": {
                  "$ref": "../v0.8/common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "type",
                "panel"
              ],
              "additionalProperties": false
            }
          ]
        }
      },
      "required": [
        "id",
        "label",
        "action"
      ],
      "additionalProperties": false,
      "description": "Asset-level character interaction: only say and panel, which reference nothing in a world."
    },
    "seatAnchor": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "../v0.8/common.schema.json#/$defs/slug"
        },
        "position": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        },
        "yaw": {
          "type": "number",
          "maximum": 360,
          "minimum": -360
        },
        "height": {
          "type": "number",
          "maximum": 1,
          "minimum": 0.2
        },
        "use": {
          "type": "array",
          "items": {
            "enum": [
              "sit"
            ]
          },
          "minItems": 1,
          "maxItems": 1,
          "uniqueItems": true
        },
        "approach": {
          "type": "number",
          "maximum": 2,
          "minimum": 0.3
        }
      },
      "required": [
        "id",
        "position",
        "yaw",
        "height"
      ],
      "additionalProperties": false,
      "description": "Where the sitter's root stands at the start of sit (asset space), facing yaw degrees about +Y from +Z. perch, lean and ground uses are reserved."
    }
  }
})schema";
inline constexpr const char *librarySchema09 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.9/library.schema.json",
  "title": "Hyprune object library manifest 0.9 (library.json, RFC-0011)",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.9"
    },
    "id": {
      "$ref": "../v0.8/common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "../v0.8/common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "description": {
      "type": "string",
      "maxLength": 512
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "../v0.8/common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1,
      "maxItems": 256
    },
    "objects": {
      "type": "array",
      "items": {
        "$ref": "../v0.8/common.schema.json#/$defs/slug"
      },
      "minItems": 1,
      "maxItems": 256,
      "uniqueItems": true
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "attribution",
    "objects"
  ],
  "additionalProperties": false
})schema";
inline constexpr const char *objectSchema010 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.10/object.schema.json",
  "title": "Hyprune object manifest 0.10 (DRAFT until world format 0.10): adds the mover trait, the RFC-0013 machine trait and the slot additions",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.10"
    },
    "id": {
      "$ref": "../v0.8/common.schema.json#/$defs/slug"
    },
    "version": {
      "$ref": "../v0.8/common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "description": {
      "type": "string",
      "maxLength": 512
    },
    "category": {
      "enum": [
        "furniture",
        "lighting",
        "plants",
        "signage",
        "machines",
        "decor",
        "characters",
        "vehicles",
        "other"
      ]
    },
    "tags": {
      "type": "array",
      "items": {
        "$ref": "../v0.8/common.schema.json#/$defs/slug"
      },
      "minItems": 0,
      "maxItems": 8,
      "uniqueItems": true
    },
    "scene": {
      "allOf": [
        {
          "$ref": "../v0.8/common.schema.json#/$defs/path"
        },
        {
          "type": "string",
          "pattern": "\\.(gltf|glb)$"
        }
      ]
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "../v0.8/common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1,
      "maxItems": 64
    },
    "bounds": {
      "type": "object",
      "properties": {
        "min": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        },
        "max": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        }
      },
      "required": [
        "min",
        "max"
      ],
      "additionalProperties": false,
      "description": "Rest-pose AABB in asset space (metres, +Y up, front +Z)."
    },
    "thumbnail": {
      "allOf": [
        {
          "$ref": "../v0.8/common.schema.json#/$defs/path"
        },
        {
          "type": "string",
          "pattern": "\\.png$"
        }
      ],
      "description": "PNG, 256 x 256, at most 256 KiB."
    },
    "lighting": {
      "type": "object",
      "properties": {
        "mode": {
          "enum": [
            "probe",
            "unlit"
          ]
        }
      },
      "required": [
        "mode"
      ],
      "additionalProperties": false
    },
    "traits": {
      "type": "object",
      "properties": {
        "decorative": {
          "type": "object",
          "properties": {
            "animations": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/animation"
              },
              "minItems": 1,
              "maxItems": 16
            }
          },
          "required": [
            "animations"
          ],
          "additionalProperties": false
        },
        "collidable": {
          "type": "object",
          "properties": {
            "node": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            }
          },
          "required": [
            "node"
          ],
          "additionalProperties": false,
          "description": "The 0.8 static collision proxy: a mesh node, not animated, not in an animated subtree."
        },
        "movable": {
          "type": "object",
          "properties": {
            "surfaces": {
              "type": "array",
              "items": {
                "enum": [
                  "floor",
                  "wall",
                  "ceiling",
                  "any"
                ]
              },
              "minItems": 1,
              "maxItems": 4,
              "uniqueItems": true
            },
            "upright": {
              "type": "boolean"
            },
            "scale": {
              "type": "array",
              "items": {
                "type": "number",
                "maximum": 4,
                "minimum": 0.25
              },
              "minItems": 2,
              "maxItems": 2
            }
          },
          "additionalProperties": false,
          "description": "Offered for user placement. Defaults: surfaces [\"floor\"], upright true for floor and ceiling, scale [1, 1]."
        },
        "character": {
          "type": "object",
          "properties": {
            "clips": {
              "type": "object",
              "propertyNames": {
                "type": "string",
                "minLength": 1,
                "maxLength": 64,
                "pattern": "^[a-z][a-z0-9_]*$"
              },
              "properties": {
                "walk": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                },
                "idle": {
                  "type": "object",
                  "properties": {
                    "speed": {
                      "type": "number",
                      "maximum": 15,
                      "exclusiveMinimum": 0.05
                    },
                    "seatHeight": {
                      "type": "number",
                      "maximum": 1,
                      "minimum": 0.2
                    }
                  },
                  "additionalProperties": false,
                  "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
                }
              },
              "additionalProperties": {
                "type": "object",
                "properties": {
                  "speed": {
                    "type": "number",
                    "maximum": 15,
                    "exclusiveMinimum": 0.05
                  },
                  "seatHeight": {
                    "type": "number",
                    "maximum": 1,
                    "minimum": 0.2
                  }
                },
                "additionalProperties": false,
                "description": "Values the import step measured; speed must match the clip's extras.speed within 2%."
              },
              "required": [
                "walk",
                "idle"
              ],
              "maxProperties": 64
            },
            "height": {
              "type": "number",
              "maximum": 2.2,
              "minimum": 1.2
            },
            "radius": {
              "type": "number",
              "maximum": 0.5,
              "minimum": 0.25
            },
            "accent": {
              "type": "boolean"
            },
            "interactions": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/interaction"
              },
              "minItems": 1,
              "maxItems": 8
            }
          },
          "required": [
            "clips",
            "height"
          ],
          "additionalProperties": false,
          "description": "RFC-0010 character: clips with measured values (walk and idle required), height and radius in metres."
        },
        "seat": {
          "type": "object",
          "properties": {
            "anchors": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/seatAnchor"
              },
              "minItems": 1,
              "maxItems": 8
            },
            "capacity": {
              "type": "integer",
              "minimum": 1,
              "maximum": 8
            }
          },
          "required": [
            "anchors"
          ],
          "additionalProperties": false
        },
        "mover": {
          "type": "object",
          "properties": {
            "speed": {
              "type": "number",
              "maximum": 30,
              "exclusiveMinimum": 0.05
            },
            "accel": {
              "type": "number",
              "maximum": 20,
              "minimum": 0.1
            },
            "bank": {
              "type": "object",
              "properties": {
                "max": {
                  "type": "number",
                  "maximum": 45,
                  "minimum": 0
                },
                "gain": {
                  "type": "number",
                  "maximum": 10,
                  "minimum": 0
                }
              },
              "required": [
                "max",
                "gain"
              ],
              "additionalProperties": false
            },
            "pitch": {
              "enum": [
                "follow",
                "level"
              ]
            },
            "offset": {
              "$ref": "../v0.8/common.schema.json#/$defs/vec3"
            },
            "item": {
              "type": "boolean"
            }
          },
          "required": [
            "speed"
          ],
          "additionalProperties": false,
          "description": "Rides track paths; item: true makes it a pooled item (at most 2,000 triangles, 1 MiB)."
        },
        "machine": {
          "type": "object",
          "properties": {
            "primary": {
              "type": "string",
              "minLength": 1,
              "maxLength": 32,
              "pattern": "^[a-z][A-Za-z0-9]*$"
            },
            "inputs": {
              "type": "array",
              "items": {
                "$ref": "#/$defs/input"
              },
              "minItems": 1,
              "maxItems": 16
            },
            "drives": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "input": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 40,
                    "pattern": "^[a-z][A-Za-z0-9]*(\\.[1-9][0-9]?)?$"
                  },
                  "target": {
                    "type": "string",
                    "maxLength": 160,
                    "pattern": "^(animation|state|emission|label|visible|count|spawn|effect|position|level):[^:\\s]{1,128}$"
                  },
                  "map": {
                    "$ref": "#/$defs/mapping"
                  }
                },
                "required": [
                  "input",
                  "target"
                ],
                "additionalProperties": false
              },
              "minItems": 0,
              "maxItems": 32
            },
            "positions": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "node": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 128
                  },
                  "range": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "maximum": 4,
                      "minimum": 0
                    },
                    "minItems": 2,
                    "maxItems": 2
                  }
                },
                "required": [
                  "node",
                  "range"
                ],
                "additionalProperties": false
              },
              "minItems": 1,
              "maxItems": 16
            },
            "recipes": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "name": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 64,
                    "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
                  },
                  "fields": {
                    "type": "object",
                    "propertyNames": {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 40,
                      "pattern": "^[a-z][A-Za-z0-9]*(\\.[1-9][0-9]?)?$"
                    },
                    "additionalProperties": {
                      "oneOf": [
                        {
                          "type": "string",
                          "maxLength": 256,
                          "pattern": "^(/.*)?$"
                        },
                        {
                          "type": "object",
                          "properties": {
                            "length": {
                              "type": "string",
                              "maxLength": 256,
                              "pattern": "^(/.*)?$"
                            }
                          },
                          "required": [
                            "length"
                          ],
                          "additionalProperties": false
                        },
                        {
                          "type": "object",
                          "properties": {
                            "countWhere": {
                              "type": "object",
                              "properties": {
                                "array": {
                                  "type": "string",
                                  "maxLength": 256,
                                  "pattern": "^(/.*)?$"
                                },
                                "pointer": {
                                  "type": "string",
                                  "maxLength": 256,
                                  "pattern": "^(/.*)?$"
                                },
                                "equals": {
                                  "anyOf": [
                                    {
                                      "type": "string",
                                      "maxLength": 256
                                    },
                                    {
                                      "type": "number"
                                    },
                                    {
                                      "type": "boolean"
                                    }
                                  ]
                                }
                              },
                              "required": [
                                "array",
                                "pointer",
                                "equals"
                              ],
                              "additionalProperties": false
                            }
                          },
                          "required": [
                            "countWhere"
                          ],
                          "additionalProperties": false
                        },
                        {
                          "type": "object",
                          "properties": {
                            "first": {
                              "type": "object",
                              "properties": {
                                "array": {
                                  "type": "string",
                                  "maxLength": 256,
                                  "pattern": "^(/.*)?$"
                                },
                                "pointer": {
                                  "type": "string",
                                  "maxLength": 256,
                                  "pattern": "^(/.*)?$"
                                },
                                "equals": {
                                  "anyOf": [
                                    {
                                      "type": "string",
                                      "maxLength": 256
                                    },
                                    {
                                      "type": "number"
                                    },
                                    {
                                      "type": "boolean"
                                    }
                                  ]
                                },
                                "select": {
                                  "type": "string",
                                  "maxLength": 256,
                                  "pattern": "^(/.*)?$"
                                }
                              },
                              "required": [
                                "array",
                                "pointer",
                                "equals",
                                "select"
                              ],
                              "additionalProperties": false
                            }
                          },
                          "required": [
                            "first"
                          ],
                          "additionalProperties": false
                        }
                      ]
                    },
                    "minProperties": 1,
                    "maxProperties": 16
                  },
                  "maps": {
                    "type": "object",
                    "propertyNames": {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 40,
                      "pattern": "^[a-z][A-Za-z0-9]*(\\.[1-9][0-9]?)?$"
                    },
                    "additionalProperties": {
                      "$ref": "#/$defs/mapping"
                    },
                    "maxProperties": 16
                  }
                },
                "required": [
                  "name",
                  "fields"
                ],
                "additionalProperties": false
              },
              "minItems": 1,
              "maxItems": 8
            }
          },
          "required": [
            "inputs",
            "drives"
          ],
          "additionalProperties": false,
          "description": "RFC-0013 machine (draft): typed inputs, drives from inputs to slot targets, value-driven positions and source recipes."
        },
        "mount": {
          "description": "Reserved (RFC-0011 P4): a prop that holds a window. Rejected in object format 0.10.",
          "not": {}
        },
        "interactive": {
          "description": "Reserved (RFC-0011 P4): use actions on ordinary objects. Rejected in object format 0.10.",
          "not": {}
        }
      },
      "additionalProperties": false,
      "description": "Trait name to trait data. Unknown or reserved names reject the object."
    },
    "slots": {
      "type": "object",
      "properties": {
        "states": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "materials": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "palette": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "id": {
                      "$ref": "../v0.8/common.schema.json#/$defs/slug"
                    },
                    "colour": {
                      "type": "array",
                      "items": {
                        "type": "number",
                        "maximum": 1,
                        "minimum": 0
                      },
                      "minItems": 3,
                      "maxItems": 3,
                      "description": "Linear RGB, 0-1."
                    },
                    "emission": {
                      "type": "number",
                      "maximum": 16,
                      "minimum": 0
                    }
                  },
                  "required": [
                    "id",
                    "colour",
                    "emission"
                  ],
                  "additionalProperties": false
                },
                "minItems": 1,
                "maxItems": 16
              }
            },
            "required": [
              "id",
              "palette"
            ],
            "additionalProperties": false,
            "oneOf": [
              {
                "properties": {
                  "materials": {
                    "type": "array"
                  }
                },
                "required": [
                  "materials"
                ]
              },
              {
                "properties": {
                  "nodes": {
                    "type": "array"
                  }
                },
                "required": [
                  "nodes"
                ]
              }
            ]
          },
          "minItems": 1,
          "maxItems": 8
        },
        "labels": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "size": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 16,
                  "exclusiveMinimum": 0
                },
                "minItems": 2,
                "maxItems": 2
              },
              "maxChars": {
                "type": "integer",
                "minimum": 1,
                "maximum": 64
              },
              "font": {
                "enum": [
                  "sans",
                  "mono"
                ]
              },
              "align": {
                "enum": [
                  "left",
                  "center",
                  "right"
                ]
              },
              "colour": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 1,
                  "minimum": 0
                },
                "minItems": 3,
                "maxItems": 3,
                "description": "Linear RGB, 0-1."
              },
              "plate": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 1,
                  "minimum": 0
                },
                "minItems": 3,
                "maxItems": 3,
                "description": "Linear RGB, 0-1."
              }
            },
            "required": [
              "id",
              "pose",
              "size",
              "maxChars"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "spawns": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "track": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "items": {
                "type": "array",
                "items": {
                  "$ref": "../v0.8/common.schema.json#/$defs/slug"
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              },
              "maxRate": {
                "type": "number",
                "maximum": 10,
                "exclusiveMinimum": 0
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "effects": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "kind": {
                "enum": [
                  "burst",
                  "pulse",
                  "flash",
                  "ring"
                ]
              },
              "colour": {
                "type": "array",
                "items": {
                  "type": "number",
                  "maximum": 1,
                  "minimum": 0
                },
                "minItems": 3,
                "maxItems": 3,
                "description": "Linear RGB, 0-1."
              },
              "durationMs": {
                "type": "integer",
                "minimum": 200,
                "maximum": 3000
              }
            },
            "required": [
              "id",
              "pose"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "tracks": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "points": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "position": {
                      "$ref": "../v0.8/common.schema.json#/$defs/vec3"
                    },
                    "type": {
                      "enum": [
                        "line",
                        "curve",
                        "arc"
                      ]
                    }
                  },
                  "required": [
                    "position",
                    "type"
                  ],
                  "additionalProperties": false
                },
                "minItems": 2,
                "maxItems": 32
              },
              "speed": {
                "type": "number",
                "maximum": 30,
                "minimum": 0.1
              },
              "items": {
                "type": "object",
                "properties": {
                  "assets": {
                    "type": "array",
                    "items": {
                      "$ref": "../v0.8/common.schema.json#/$defs/slug"
                    },
                    "minItems": 1,
                    "maxItems": 16,
                    "uniqueItems": true
                  },
                  "max": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 256
                  },
                  "spacing": {
                    "type": "number",
                    "maximum": 10,
                    "exclusiveMinimum": 0
                  },
                  "overflow": {
                    "enum": [
                      "refuse",
                      "drop-oldest"
                    ]
                  }
                },
                "required": [
                  "assets",
                  "max"
                ],
                "additionalProperties": false
              },
              "sink": {
                "type": "boolean"
              }
            },
            "required": [
              "id",
              "points"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 4
        },
        "stacks": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "node": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "cap": {
                "type": "integer",
                "minimum": 1,
                "maximum": 100
              },
              "layout": {
                "type": "object",
                "properties": {
                  "grid": {
                    "type": "array",
                    "items": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 64
                    },
                    "minItems": 3,
                    "maxItems": 3
                  },
                  "spacing": {
                    "type": "array",
                    "items": {
                      "type": "number",
                      "maximum": 10,
                      "minimum": -10
                    },
                    "minItems": 3,
                    "maxItems": 3,
                    "description": "Metres between copies per axis; a negative component fills from the node towards -axis. Non-zero where that axis's grid is above 1."
                  },
                  "fill": {
                    "enum": [
                      "xyz",
                      "xzy",
                      "yxz",
                      "yzx",
                      "zxy",
                      "zyx"
                    ]
                  }
                },
                "required": [
                  "grid",
                  "spacing",
                  "fill"
                ],
                "additionalProperties": false
              },
              "rate": {
                "type": "number",
                "minimum": 0,
                "maximum": 1000,
                "description": "Copies per second the shown count moves toward the target; 0 or absent: at once."
              }
            },
            "required": [
              "id",
              "node",
              "layout",
              "cap"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "toggles": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "nodes": {
                "type": "array",
                "items": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "minItems": 1,
                "maxItems": 16,
                "uniqueItems": true
              }
            },
            "required": [
              "id",
              "nodes"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        },
        "levels": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "node": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "mode": {
                "enum": [
                  "fill",
                  "slide"
                ]
              },
              "axis": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -1,
                  "maximum": 1
                },
                "minItems": 3,
                "maxItems": 3
              },
              "length": {
                "type": "number",
                "exclusiveMinimum": 0,
                "maximum": 10
              },
              "surface": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "settle": {
                "enum": [
                  "ease",
                  "spring"
                ]
              }
            },
            "required": [
              "id",
              "node",
              "mode",
              "length"
            ],
            "additionalProperties": false,
            "not": {
              "properties": {
                "mode": {
                  "const": "slide"
                },
                "surface": {
                  "type": "string"
                }
              },
              "required": [
                "mode",
                "surface"
              ]
            },
            "description": "A subtree scaled (fill) or moved (slide) by level:<id>, 0-1. axis: asset-space unit vector, default [0, 1, 0]; surface: fill only; settle default ease."
          },
          "minItems": 1,
          "maxItems": 8
        },
        "ports": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "id": {
                "$ref": "../v0.8/common.schema.json#/$defs/slug"
              },
              "pose": {
                "$ref": "#/$defs/slotPose"
              },
              "direction": {
                "enum": [
                  "in",
                  "out"
                ]
              }
            },
            "required": [
              "id",
              "pose",
              "direction"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 8
        }
      },
      "additionalProperties": false,
      "description": "Authored parts that RFC-0013 drives."
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "category",
    "scene",
    "license",
    "attribution",
    "bounds",
    "traits"
  ],
  "additionalProperties": false,
  "$defs": {
    "animation": {
      "type": "object",
      "properties": {
        "node": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "type": {
          "enum": [
            "spin",
            "orbit",
            "bob"
          ]
        },
        "axis": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 1.001,
            "minimum": -1.001
          },
          "minItems": 3,
          "maxItems": 3
        },
        "pivot": {
          "type": "array",
          "items": {
            "type": "number",
            "maximum": 10000,
            "minimum": -10000
          },
          "minItems": 3,
          "maxItems": 3
        },
        "amplitude": {
          "type": "number",
          "maximum": 10,
          "exclusiveMinimum": 0
        },
        "period": {
          "type": "number",
          "maximum": 3600,
          "minimum": 0.5
        },
        "phase": {
          "type": "number",
          "minimum": 0,
          "exclusiveMaximum": 1
        }
      },
      "required": [
        "node",
        "type",
        "axis",
        "period"
      ],
      "additionalProperties": false,
      "description": "A world 0.8 declarative node animation (spin, orbit or bob)."
    },
    "slotPose": {
      "$ref": "../v0.8/common.schema.json#/$defs/pose"
    },
    "interaction": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "../v0.8/common.schema.json#/$defs/slug"
        },
        "label": {
          "type": "string",
          "minLength": 1,
          "maxLength": 32,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "prompt": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "action": {
          "oneOf": [
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "say"
                },
                "lines": {
                  "type": "array",
                  "items": {
                    "type": "string",
                    "minLength": 1,
                    "maxLength": 160,
                    "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
                  },
                  "minItems": 1,
                  "maxItems": 8
                }
              },
              "required": [
                "type",
                "lines"
              ],
              "additionalProperties": false
            },
            {
              "type": "object",
              "properties": {
                "type": {
                  "const": "panel"
                },
                "panel": {
                  "$ref": "../v0.8/common.schema.json#/$defs/slug"
                }
              },
              "required": [
                "type",
                "panel"
              ],
              "additionalProperties": false
            }
          ]
        }
      },
      "required": [
        "id",
        "label",
        "action"
      ],
      "additionalProperties": false,
      "description": "Asset-level character interaction: only say and panel, which reference nothing in a world."
    },
    "seatAnchor": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "../v0.8/common.schema.json#/$defs/slug"
        },
        "position": {
          "$ref": "../v0.8/common.schema.json#/$defs/vec3"
        },
        "yaw": {
          "type": "number",
          "maximum": 360,
          "minimum": -360
        },
        "height": {
          "type": "number",
          "maximum": 1,
          "minimum": 0.2
        },
        "use": {
          "type": "array",
          "items": {
            "enum": [
              "sit"
            ]
          },
          "minItems": 1,
          "maxItems": 1,
          "uniqueItems": true
        },
        "approach": {
          "type": "number",
          "maximum": 2,
          "minimum": 0.3
        }
      },
      "required": [
        "id",
        "position",
        "yaw",
        "height"
      ],
      "additionalProperties": false,
      "description": "Where the sitter's root stands at the start of sit (asset space), facing yaw degrees about +Y from +Z. perch, lean and ground uses are reserved."
    },
    "mapping": {
      "type": "object",
      "properties": {
        "cases": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "equals": {
                "anyOf": [
                  {
                    "type": "string",
                    "maxLength": 256
                  },
                  {
                    "type": "number"
                  },
                  {
                    "type": "boolean"
                  }
                ]
              },
              "in": {
                "type": "array",
                "items": {
                  "anyOf": [
                    {
                      "type": "string",
                      "maxLength": 256
                    },
                    {
                      "type": "number"
                    },
                    {
                      "type": "boolean"
                    }
                  ]
                },
                "minItems": 1,
                "maxItems": 16
              },
              "to": {
                "anyOf": [
                  {
                    "type": "string",
                    "maxLength": 256
                  },
                  {
                    "type": "number"
                  },
                  {
                    "type": "boolean"
                  }
                ]
              }
            },
            "required": [
              "to"
            ],
            "additionalProperties": false,
            "oneOf": [
              {
                "properties": {
                  "equals": {}
                },
                "required": [
                  "equals"
                ]
              },
              {
                "properties": {
                  "in": {}
                },
                "required": [
                  "in"
                ]
              }
            ]
          },
          "minItems": 1,
          "maxItems": 16
        },
        "thresholds": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "below": {
                "type": "number"
              },
              "atLeast": {
                "type": "number"
              },
              "to": {
                "anyOf": [
                  {
                    "type": "string",
                    "maxLength": 256
                  },
                  {
                    "type": "number"
                  },
                  {
                    "type": "boolean"
                  }
                ]
              }
            },
            "required": [
              "to"
            ],
            "additionalProperties": false,
            "oneOf": [
              {
                "properties": {
                  "below": {}
                },
                "required": [
                  "below"
                ]
              },
              {
                "properties": {
                  "atLeast": {}
                },
                "required": [
                  "atLeast"
                ]
              }
            ]
          },
          "minItems": 1,
          "maxItems": 16
        },
        "else": {
          "anyOf": [
            {
              "type": "string",
              "maxLength": 256
            },
            {
              "type": "number"
            },
            {
              "type": "boolean"
            }
          ]
        },
        "scale": {
          "type": "object",
          "properties": {
            "from": {
              "type": "array",
              "items": {
                "type": "number"
              },
              "minItems": 2,
              "maxItems": 2
            },
            "to": {
              "type": "array",
              "items": {
                "type": "number"
              },
              "minItems": 2,
              "maxItems": 2
            }
          },
          "required": [
            "from",
            "to"
          ],
          "additionalProperties": false
        },
        "clamp": {
          "type": "boolean"
        },
        "round": {
          "enum": [
            "floor",
            "ceil",
            "nearest"
          ]
        },
        "becomes": {
          "anyOf": [
            {
              "type": "string",
              "maxLength": 256
            },
            {
              "type": "number"
            },
            {
              "type": "boolean"
            }
          ]
        },
        "from": {
          "anyOf": [
            {
              "type": "string",
              "maxLength": 256
            },
            {
              "type": "number"
            },
            {
              "type": "boolean"
            }
          ]
        },
        "counter": {
          "const": true
        },
        "format": {
          "type": "object",
          "properties": {
            "decimals": {
              "type": "integer",
              "minimum": 0,
              "maximum": 6
            },
            "compact": {
              "type": "boolean"
            },
            "unit": {
              "type": "string",
              "maxLength": 16,
              "pattern": "^[^\\u0000-\\u001f\\u007f]*$"
            }
          },
          "additionalProperties": false
        },
        "average": {
          "type": "object",
          "properties": {
            "windowS": {
              "type": "number",
              "minimum": 1,
              "maximum": 3600
            },
            "override": {
              "type": "string",
              "minLength": 1,
              "maxLength": 40,
              "pattern": "^[a-z][A-Za-z0-9]*(\\.[1-9][0-9]?)?$"
            }
          },
          "additionalProperties": false,
          "description": "Running average modifier: the time-weighted mean of the value over the last windowS seconds (default 60) of fresh data; the mapping's form applies to it. override (drives only): another non-event input whose value, while present, replaces the average."
        }
      },
      "additionalProperties": false,
      "description": "RFC-0013 mapping language: direct ({}), cases, thresholds, scale, becomes, counter or format; average modifies the value forms (not becomes or counter).",
      "not": {
        "anyOf": [
          {
            "properties": {
              "average": {
                "type": "object"
              },
              "becomes": {}
            },
            "required": [
              "average",
              "becomes"
            ]
          },
          {
            "properties": {
              "average": {
                "type": "object"
              },
              "counter": {
                "const": true
              }
            },
            "required": [
              "average",
              "counter"
            ]
          }
        ]
      }
    },
    "input": {
      "type": "object",
      "properties": {
        "id": {
          "type": "string",
          "minLength": 1,
          "maxLength": 32,
          "pattern": "^[a-z][A-Za-z0-9]*$"
        },
        "type": {
          "enum": [
            "enum",
            "int",
            "number",
            "bool",
            "text",
            "event",
            "items"
          ]
        },
        "label": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "explain": {
          "type": "string",
          "minLength": 1,
          "maxLength": 256,
          "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
        },
        "default": {
          "anyOf": [
            {
              "type": "string",
              "maxLength": 256
            },
            {
              "type": "number"
            },
            {
              "type": "boolean"
            }
          ]
        },
        "repeat": {
          "type": "integer",
          "minimum": 2,
          "maximum": 16
        },
        "values": {
          "type": "array",
          "items": {
            "$ref": "../v0.8/common.schema.json#/$defs/slug"
          },
          "minItems": 1,
          "maxItems": 16,
          "uniqueItems": true
        },
        "min": {
          "type": "number"
        },
        "max": {
          "type": "number"
        },
        "maxLength": {
          "type": "integer",
          "minimum": 1,
          "maximum": 64
        },
        "states": {
          "type": "array",
          "items": {
            "$ref": "../v0.8/common.schema.json#/$defs/slug"
          },
          "minItems": 1,
          "maxItems": 16,
          "uniqueItems": true
        }
      },
      "required": [
        "id",
        "type",
        "label",
        "explain"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "type": {
                "const": "enum"
              }
            }
          },
          "then": {
            "properties": {
              "values": {
                "type": "array"
              }
            },
            "required": [
              "values"
            ]
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "enum": [
                  "int",
                  "number"
                ]
              }
            }
          },
          "then": {
            "properties": {
              "min": {
                "type": "number"
              },
              "max": {
                "type": "number"
              }
            },
            "required": [
              "min",
              "max"
            ]
          }
        },
        {
          "if": {
            "properties": {
              "type": {
                "const": "items"
              }
            }
          },
          "then": {
            "properties": {
              "states": {
                "type": "array"
              }
            },
            "required": [
              "states"
            ]
          }
        }
      ],
      "description": "A typed machine input (RFC-0013). repeat makes indexed inputs id.1 ... id.N."
    }
  }
})schema";
inline constexpr const char *librarySchema010 = R"schema({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://hyprune.com/schema/v0.10/library.schema.json",
  "title": "Hyprune object library manifest 0.10 (library.json, RFC-0011) (DRAFT until world format 0.10)",
  "type": "object",
  "properties": {
    "formatVersion": {
      "const": "0.10"
    },
    "id": {
      "$ref": "../v0.8/common.schema.json#/$defs/id"
    },
    "version": {
      "$ref": "../v0.8/common.schema.json#/$defs/version"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "description": {
      "type": "string",
      "maxLength": 512
    },
    "license": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256
    },
    "attribution": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "anyOf": [
              {
                "$ref": "../v0.8/common.schema.json#/$defs/path"
              },
              {
                "const": "."
              }
            ]
          },
          "author": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "license": {
            "type": "string",
            "minLength": 1,
            "maxLength": 256
          },
          "source": {
            "type": "string",
            "minLength": 1,
            "maxLength": 2048,
            "pattern": "^https://"
          }
        },
        "required": [
          "path",
          "author",
          "license"
        ],
        "additionalProperties": false
      },
      "minItems": 1,
      "maxItems": 256
    },
    "objects": {
      "type": "array",
      "items": {
        "$ref": "../v0.8/common.schema.json#/$defs/slug"
      },
      "minItems": 1,
      "maxItems": 256,
      "uniqueItems": true
    }
  },
  "required": [
    "formatVersion",
    "id",
    "version",
    "name",
    "license",
    "attribution",
    "objects"
  ],
  "additionalProperties": false
})schema";
} // namespace hyprune::world
