# ColorAlarm Web Frontend

Mithril + Vite SPA for managing alarms through the ESP32 REST API.

## Features

- List alarms
- Create alarms
- Edit alarms
- Delete alarms
- Toggle alarm `enabled` state
- Manage vacation mode (`alarm` or `do_nothing`)

## REST Contract

- `GET /api/alarms`
- `POST /api/alarms`
- `PUT /api/alarms/:id`
- `DELETE /api/alarms/:id`
- `GET /api/vacation-mode`
- `PUT /api/vacation-mode`

Alarm object shape (responses):

```json
{
  "id": "weekday",
  "day": 1,
  "hour": 6,
  "minute": 45,
  "second": 0,
  "enabled": true
}
```

Create payload (`POST /api/alarms`) omits `id` and the server generates a UUID:

```json
{
  "day": 1,
  "hour": 6,
  "minute": 45,
  "second": 0,
  "enabled": true
}
```

Vacation mode payload:

```json
{
  "enabled": true,
  "option": "alarm",
  "hour": 7,
  "minute": 15,
  "second": 0
}
```

## Local Development

1. Install dependencies:

```bash
npm install
```

2. Start the mock API server:

```bash
npm run mock
```

3. In another shell, start Vite:

```bash
npm run dev
```

Vite proxies `/api/*` to the mock server at `http://localhost:3001`.

## Targeting Real ESP32 Hardware

Set `VITE_API_BASE_URL` in your shell before starting Vite:

```bash
export VITE_API_BASE_URL=http://<esp32-ip>
npm run dev
```

If `VITE_API_BASE_URL` is set, requests are sent directly to the device.

## Embedding in Firmware (SPIFFS)

To serve the built UI from the ESP32 flash:

```bash
npm install
npm run build
```

Then from project root run:

```bash
idf.py build flash
```

The project CMake config packages `dist/` into the `spiffs` partition image.
