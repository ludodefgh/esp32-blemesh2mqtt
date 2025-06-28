import yaml
import os

FILES = [
    "mesh_model_uuids.yaml",
    "mmdl_model_uuids.yaml"
]

def parse_models(filepath):
    with open(filepath, "r") as f:
        return yaml.safe_load(f)

def sanitize(name):
    return name.replace('"', '\\"').replace('\n', ' ')

def generate_table(entries):
    output = []
    for entry in entries:
        name = entry.get("name", "Unnamed").strip()
        uuid = entry.get("uuid")
        if uuid is None:
            continue
        try:
            model_id = int(str(uuid), 0)
            output.append((model_id, sanitize(name)))
        except ValueError:
            continue
    return output

def main():
    all_models = []

    for file in FILES:
        if not os.path.exists(file):
            print(f"File not found: {file}")
            continue
        parsed = parse_models(file)
        key = next(iter(parsed))
        entries = parsed[key]
        all_models.extend(generate_table(entries))

    # Generate model_map.h
    output_dir = "../sig_models"
    os.makedirs(output_dir, exist_ok=True)

    with open(os.path.join(output_dir, "model_map.h"), "w") as h:
        h.write("""#pragma once
    #include <stdint.h>

    typedef struct {
        uint32_t model_id;
       const char * const name;
    } model_map_entry_t;

    const char *lookup_model_name(uint32_t model_id);
    """)

    with open(os.path.join(output_dir, "model_map.cpp"), "w") as c:
        c.write('#include "model_map.h"\n\n')
        c.write("static constexpre model_map_entry_t model_map[] = {\n")
        for model_id, name in sorted(all_models):
            c.write(f'    {{ 0x{model_id:04X}, "{name}" }},\n')
        c.write('    { 0x0000, NULL } // sentinel\n')
        c.write("};\n\n")
        c.write("""const char *lookup_model_name(uint32_t model_id) {
        for (int i = 0; model_map[i].name != NULL; ++i) {
            if (model_map[i].model_id == model_id) {
                return model_map[i].name;
            }
        }
        return "Unknown Model";
    }
    """)


if __name__ == "__main__":
    main()
