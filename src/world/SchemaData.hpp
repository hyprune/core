#pragma once
namespace hyprune::world {
inline constexpr const char* worldSchema=R"schema({
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
}
)schema";
inline constexpr const char* commonSchema=R"schema({
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
}
)schema";
}
