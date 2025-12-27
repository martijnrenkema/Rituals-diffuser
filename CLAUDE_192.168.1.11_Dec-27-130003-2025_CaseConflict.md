# Rituals Perfume Genie 2.0 - Dev Notes

**GitHub:** https://github.com/martijnrenkema/Rituals-diffuser
**Version:** v1.1.0

## Quick Commands
```bash
pio run -e esp8266                    # Build
pio run -e esp8266_ota -t upload      # OTA flash
```

## Defaults
- AP Password: diffuser123
- OTA Password: diffuser-ota
- AP IP: 192.168.4.1

## API
- /api/status, /api/fan, /api/wifi, /api/mqtt
- /api/rfid, /api/night, /api/passwords, /api/reset

## Backup Original FW
```bash
esptool.py --port /dev/cu.usbserial-XXXX read_flash 0x00000 0x200000 rituals_original.bin
```
