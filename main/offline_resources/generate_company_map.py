import yaml
import os

INPUT_FILE = "company_identifiers.yaml"
OUTPUT_DIR = "../sig_companies"

def parse_yaml(filepath):
    with open(filepath, "r") as f:
        return yaml.safe_load(f)

def sanitize(name):
    return name.replace('"', '\\"').replace('\n', ' ')

def main():
    data = parse_yaml(INPUT_FILE)
    entries = data.get("company_identifiers", [])

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    with open(os.path.join(OUTPUT_DIR, "company_map.h"), "w") as h:
        h.write("""#pragma once
#include <stdint.h>

const char *lookup_company_name(uint16_t company_id);
""")

    with open(os.path.join(OUTPUT_DIR, "company_map.cpp"), "w") as c:
        c.write('#include "company_map.h"\n\n')
        c.write("typedef struct {\n    uint16_t id;\n    const char * const name;\n} company_entry_t;\n\n")
        c.write("static constexpr company_entry_t company_map[] = {\n")

        for entry in entries:
            try:
                cid = int(str(entry.get("value")), 0)
                name = sanitize(str(entry.get("name", "Unknown")))
                c.write(f'    {{ 0x{cid:04X}, "{name}" }},\n')
            except Exception as e:
                print(f"Skipping entry: {e}")

        c.write('    { 0x0000, NULL }\n};\n\n')
        c.write("""const char *lookup_company_name(uint16_t company_id) {
    for (int i = 0; company_map[i].name != NULL; ++i) {
        if (company_map[i].id == company_id) {
            return company_map[i].name;
        }
    }
    return "Unknown Company";
}
""")

if __name__ == "__main__":
    main()
