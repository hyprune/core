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
} // namespace hyprune::world
