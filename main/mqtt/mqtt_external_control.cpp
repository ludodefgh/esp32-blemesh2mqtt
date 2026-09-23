#include "mqtt_external_control.h"

// Standard C/C++ libraries
#include <cstdlib>
#include <cstring>

// ESP-IDF includes
#include "cJSON.h"

// Project includes
#include "ble_mesh/ble_mesh_control.h"
#include "common/log_common.h"
#include "mqtt_bridge.h"
#include "mqtt_control.h"
#include "mqtt_credentials.h"

#define TAG "APP_MQTT_EXT"

static std::string external_node_id(uint16_t addr)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "ext_%04X", addr);
    return buf;
}

static std::string external_node_base_topic(uint16_t addr)
{
    return get_bridge_base_topic() + "/" + external_node_id(addr);
}

static bool external_node_is_cover(const external_mesh_node_t &node)
{
    const uint16_t light_features = FEATURE_LIGHT_LIGHTNESS | FEATURE_LIGHT_HSL | FEATURE_LIGHT_CTL;
    return (node.features & FEATURE_GENERIC_LEVEL) && !(node.features & light_features);
}

static std::string external_node_discovery_id(const external_mesh_node_t &node)
{
    const std::string id = external_node_id(node.unicast);
    if (external_node_is_cover(node))
    {
        return "homeassistant/cover/blemesh2mqtt_" + id + "_cover/config";
    }
    return "homeassistant/light/blemesh2mqtt_" + id + "_light/config";
}

static CJsonPtr make_external_discovery_message(const external_mesh_node_t &node)
{
    cJSON *root = cJSON_CreateObject();
    const std::string id = external_node_id(node.unicast);

    cJSON *dev = nullptr;
    cJSON_AddItemToObject(root, "dev", dev = cJSON_CreateObject());
    if (dev)
    {
        char name[32];
        snprintf(name, sizeof(name), "External Node %04X", node.unicast);
        cJSON_AddItemToObject(dev, "name", cJSON_CreateString(name));
        cJSON_AddItemToObject(dev, "ids", cJSON_CreateString(id.c_str()));
        cJSON_AddItemToObject(dev, "via_device", cJSON_CreateString(get_bridge_mac_identifier().c_str()));
    }

    cJSON *origin = nullptr;
    cJSON_AddItemToObject(root, "o", origin = cJSON_CreateObject());
    if (origin)
    {
        cJSON_AddItemToObject(origin, "name", cJSON_CreateString("blemesh2mqtt"));
        cJSON_AddItemToObject(origin, "sw", cJSON_CreateString("0.0.1"));
    }

    const std::string root_topic = external_node_base_topic(node.unicast);
    cJSON_AddItemToObject(root, "~", cJSON_CreateString(root_topic.c_str()));
    cJSON_AddItemToObject(root, "name", cJSON_CreateNull());
    cJSON_AddItemToObject(root, "cmd_t", cJSON_CreateString("~/set"));
    cJSON_AddItemToObject(root, "stat_t", cJSON_CreateString("~/state"));

    if (external_node_is_cover(node))
    {
        cJSON_AddItemToObject(root, "uniq_id", cJSON_CreateString((id + "_cover").c_str()));
        cJSON_AddItemToObject(root, "pos_t", cJSON_CreateString("~/state"));
        cJSON_AddItemToObject(root, "set_pos_t", cJSON_CreateString("~/set_position"));
        cJSON_AddItemToObject(root, "pos_template", cJSON_CreateString("{{ value_json.position }}"));
        cJSON_AddItemToObject(root, "value_template", cJSON_CreateString("{{ value_json.state }}"));
        cJSON_AddItemToObject(root, "device_class", cJSON_CreateString("blind"));
    }
    else
    {
        cJSON_AddItemToObject(root, "uniq_id", cJSON_CreateString((id + "_light").c_str()));
        cJSON_AddItemToObject(root, "schema", cJSON_CreateString("json"));

        const uint16_t light_features = FEATURE_LIGHT_LIGHTNESS | FEATURE_LIGHT_HSL | FEATURE_LIGHT_CTL;
        if (node.features & light_features)
        {
            cJSON_AddItemToObject(root, "brightness", cJSON_CreateBool(1));
            cJSON_AddNumberToObject(root, "brightness_scale", node.max_lightness);
        }

        cJSON *sup_clrm = nullptr;
        cJSON_AddItemToObject(root, "sup_clrm", sup_clrm = cJSON_CreateArray());
        if (sup_clrm != nullptr)
        {
            bool has_color = false;
            if (node.features & FEATURE_LIGHT_CTL)
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("color_temp"));
                cJSON_AddItemToObject(root, "color_temp_kelvin", cJSON_CreateBool(1));
                cJSON_AddItemToObject(root, "min_kelvin", cJSON_CreateNumber(node.min_temp));
                cJSON_AddItemToObject(root, "max_kelvin", cJSON_CreateNumber(node.max_temp));
                has_color = true;
            }
            if (node.features & FEATURE_LIGHT_HSL)
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("hs"));
                has_color = true;
            }
            if (!has_color && (node.features & FEATURE_LIGHT_LIGHTNESS))
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("brightness"));
            }
        }
    }

    return CJsonPtr{root, cJSON_Delete};
}

static CJsonPtr make_external_status_message(const external_mesh_node_t &node)
{
    cJSON *root = cJSON_CreateObject();

    if (external_node_is_cover(node))
    {
        int position = (int)map(node.level, -32768, 32767, 0, 100);
        cJSON_AddStringToObject(root, "state", position > 0 ? "open" : "closed");
        cJSON_AddNumberToObject(root, "position", position);
    }
    else
    {
        cJSON_AddStringToObject(root, "state", node.onoff ? "ON" : "OFF");

        if (node.color_mode == color_mode_t::color_temp)
        {
            cJSON_AddStringToObject(root, "color_mode", "color_temp");
            cJSON_AddNumberToObject(root, "brightness", node.lightness);
            cJSON_AddNumberToObject(root, "color_temp", node.temperature);
        }
        else if (node.color_mode == color_mode_t::hs)
        {
            cJSON_AddStringToObject(root, "color_mode", "hs");
            cJSON_AddNumberToObject(root, "brightness", node.lightness);
            cJSON *color = nullptr;
            cJSON_AddItemToObject(root, "color", color = cJSON_CreateObject());
            if (color)
            {
                cJSON_AddNumberToObject(color, "h", (uint16_t)map(node.hue, node.min_hue, node.max_hue, 0, 360));
                cJSON_AddNumberToObject(color, "s", (uint16_t)map(node.saturation, node.min_saturation, node.max_saturation, 0, 100));
            }
        }
        else if (node.features & FEATURE_LIGHT_LIGHTNESS)
        {
            cJSON_AddStringToObject(root, "color_mode", "brightness");
            cJSON_AddNumberToObject(root, "brightness", node.lightness);
        }
    }

    return CJsonPtr{root, cJSON_Delete};
}

static void mqtt_subscribe_external_node(esp_mqtt_client_handle_t client, const external_mesh_node_t &node)
{
    const std::string base = external_node_base_topic(node.unicast);
    esp_mqtt_client_subscribe(client, (base + "/set").c_str(), 0);
    if (external_node_is_cover(node))
    {
        esp_mqtt_client_subscribe(client, (base + "/set_position").c_str(), 0);
    }
}

void mqtt_subscribe_all_external_nodes(esp_mqtt_client_handle_t client)
{
    for_each_external_node([client](const external_mesh_node_t &node)
                            { mqtt_subscribe_external_node(client, node); });
}

static void mqtt_publish_external_discovery(const external_mesh_node_t &node)
{
    CJsonPtr msg = make_external_discovery_message(node);
    char *json = cJSON_PrintUnformatted(msg.get());
    esp_mqtt_client_publish(mqtt_get_client(), external_node_discovery_id(node).c_str(), json, 0, 0, 0);
    cJSON_free(json);
}

static void mqtt_publish_external_status(const external_mesh_node_t &node)
{
    CJsonPtr msg = make_external_status_message(node);
    char *json = cJSON_PrintUnformatted(msg.get());
    const std::string state_topic = external_node_base_topic(node.unicast) + "/state";
    esp_mqtt_client_publish(mqtt_get_client(), state_topic.c_str(), json, 0, 0, 0);
    cJSON_free(json);
}

void mqtt_republish_all_external_nodes(void)
{
    for_each_external_node([](const external_mesh_node_t &node)
                            {
        mqtt_subscribe_external_node(mqtt_get_client(), node);
        mqtt_publish_external_discovery(node);
        mqtt_publish_external_status(node); });
}

void mqtt_notify_external_node_changed(uint16_t addr, bool is_new)
{
    if (mqtt_credentials().get_connection_state() != mqtt_connection_state_t::CONNECTED)
    {
        return;
    }

    external_mesh_node_t node;
    if (!ble_mesh_find_external_node(addr, node))
    {
        return;
    }

    if (is_new)
    {
        LOG_INFO(TAG, "New external node 0x%04X - publishing HA discovery", addr);
        mqtt_subscribe_external_node(mqtt_get_client(), node);
        mqtt_publish_external_discovery(node);
    }
    mqtt_publish_external_status(node);
}

bool mqtt_handle_external_node_data(const std::string &topic, const char *data, int data_len)
{
    const auto pos = topic.find("ext_");
    if (pos == std::string::npos || topic.size() < pos + 4 + 4)
    {
        return false;
    }

    const std::string addr_hex = topic.substr(pos + 4, 4);
    char *endptr = nullptr;
    unsigned long addr_ul = strtoul(addr_hex.c_str(), &endptr, 16);
    if (endptr != addr_hex.c_str() + addr_hex.size())
    {
        return false;
    }
    const uint16_t addr = (uint16_t)addr_ul;

    external_mesh_node_t node;
    if (!ble_mesh_find_external_node(addr, node))
    {
        return false;
    }

    const bool is_cover = external_node_is_cover(node);

    if (is_cover && topic.find("/set_position") != std::string::npos)
    {
        const std::string pos_str{data, static_cast<size_t>(data_len)};
        char *pend = nullptr;
        long pos_l = strtol(pos_str.c_str(), &pend, 10);
        if (pend == pos_str.c_str() || *pend != '\0')
        {
            return true;
        }
        int pct = pos_l < 0 ? 0 : (pos_l > 100 ? 100 : (int)pos_l);
        ble_mesh_send_external_level_command(addr, (int16_t)map(pct, 0, 100, -32768, 32767));
        return true;
    }

    if (is_cover && topic.find("/set") != std::string::npos)
    {
        const std::string cmd{data, static_cast<size_t>(data_len)};
        if (cmd == "OPEN")
        {
            ble_mesh_send_external_level_command(addr, 32767);
        }
        else if (cmd == "CLOSE")
        {
            ble_mesh_send_external_level_command(addr, -32768);
        }
        return true;
    }

    CJsonPtr payload(cJSON_Parse(data), cJSON_Delete);
    if (!payload)
    {
        return true;
    }

    if (const cJSON *state = cJSON_GetObjectItemCaseSensitive(payload.get(), "state"))
    {
        if (cJSON_IsString(state) && state->valuestring)
        {
            if (strcmp(state->valuestring, "ON") == 0)
            {
                ble_mesh_send_external_command(addr, true);
            }
            else if (strcmp(state->valuestring, "OFF") == 0)
            {
                ble_mesh_send_external_command(addr, false);
            }
        }
    }

    const uint16_t light_features = FEATURE_LIGHT_LIGHTNESS | FEATURE_LIGHT_HSL | FEATURE_LIGHT_CTL;
    if (node.features & light_features)
    {
        if (const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(payload.get(), "brightness"))
        {
            if (cJSON_IsNumber(brightness))
            {
                double clamped = brightness->valuedouble;
                clamped = clamped < 0 ? 0 : (clamped > 65535 ? 65535 : clamped);
                ble_mesh_send_external_lightness_command(addr, (uint16_t)clamped);
            }
        }
    }

    if (node.features & FEATURE_LIGHT_HSL)
    {
        if (const cJSON *color = cJSON_GetObjectItemCaseSensitive(payload.get(), "color"))
        {
            if (cJSON_IsObject(color))
            {
                uint16_t hue = node.hue;
                uint16_t saturation = node.saturation;
                bool changed = false;

                if (const cJSON *h = cJSON_GetObjectItemCaseSensitive(color, "h"); cJSON_IsNumber(h))
                {
                    hue = (uint16_t)map(h->valuedouble, 0, 360, node.min_hue, node.max_hue);
                    changed = true;
                }
                if (const cJSON *s = cJSON_GetObjectItemCaseSensitive(color, "s"); cJSON_IsNumber(s))
                {
                    saturation = (uint16_t)map(s->valuedouble, 0, 100, node.min_saturation, node.max_saturation);
                    changed = true;
                }
                if (changed)
                {
                    ble_mesh_send_external_hsl_command(addr, hue, saturation);
                }
            }
        }
    }

    if (node.features & FEATURE_LIGHT_CTL)
    {
        if (const cJSON *color_temp = cJSON_GetObjectItemCaseSensitive(payload.get(), "color_temp"))
        {
            if (cJSON_IsNumber(color_temp))
            {
                double clamped = color_temp->valuedouble;
                clamped = clamped < node.min_temp ? node.min_temp : (clamped > node.max_temp ? node.max_temp : clamped);
                ble_mesh_send_external_ctl_command(addr, (uint16_t)clamped);
            }
        }
    }

    return true;
}
