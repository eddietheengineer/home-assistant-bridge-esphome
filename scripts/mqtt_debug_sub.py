#!/usr/bin/env python3
"""MQTT debug subscriber for GE Appliances ESPHome Bridge verification.

Usage:
    python3 scripts/mqtt_debug_sub.py -S secrets.yaml -t 'gea-esphome-zonelinec6/debug' [-f]

Options:
    -S SECRETS  Path to ESPHome secrets.yaml (required)
    -t TOPIC    MQTT topic to subscribe (required)
    -f          Filter to show only ERD-related messages
"""

import argparse
import sys

import paho.mqtt.client as mqtt


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('-S', '--secrets', required=True,
                   help='Path to ESPHome secrets.yaml')
    p.add_argument('-t', '--topic', required=True,
                   help='MQTT topic to subscribe')
    p.add_argument('-f', '--filter', action='store_true',
                   help='Show only ERD-related messages')
    return p.parse_args()


def load_secrets(path):
    """Minimal YAML parser for ESPHome secrets.yaml (key: value format)."""
    secrets = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if ':' in line:
                key, val = line.split(':', 1)
                val = val.strip().strip('"').strip("'")
                secrets[key.strip()] = val
    return secrets


ERD_KEYWORDS = [
    'erd_state_table', 'mqtt_fsm', 'geappliances_bridge',
    'Subscription activity', 'Initializing MQTT bridge',
    'Feature bit parsing', 'Registered ERD', 'drain:',
    'erd_set', 'appliance_side', 'subscription_handler',
]


def should_show(msg, do_filter):
    if not do_filter:
        return True
    return any(kw in msg for kw in ERD_KEYWORDS)


def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8', errors='replace')
    if should_show(payload, userdata):
        print(payload, flush=True)


def main():
    args = parse_args()
    secrets = load_secrets(args.secrets)

    host = secrets.get('mqtt_broker')
    user = secrets.get('mqtt_username')
    password = secrets.get('mqtt_password')

    if not host:
        print("Error: mqtt_broker not found in secrets.yaml", file=sys.stderr)
        sys.exit(1)
    if not user or not password:
        print("Error: mqtt_username/mqtt_password not found in secrets.yaml", file=sys.stderr)
        sys.exit(1)

    client = mqtt.Client()
    client.username_pw_set(user, password)
    client.connect(host, 1883, 60)
    client.user_data_set(args.filter)
    client.on_message = on_message
    client.subscribe(args.topic)
    client.loop_forever()


if __name__ == '__main__':
    main()
