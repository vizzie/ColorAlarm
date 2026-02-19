import http from "node:http";
import crypto from "node:crypto";

const PORT = Number(process.env.MOCK_PORT || 3001);

/** @type {Array<{id:string,day:number,hour:number,minute:number,second:number,enabled:boolean}>} */
const alarms = [
  { id: "weekday", day: 1, hour: 6, minute: 45, second: 0, enabled: true },
  { id: "weekend", day: 0, hour: 7, minute: 30, second: 0, enabled: false }
];

let vacationMode = {
  enabled: false,
  option: "do_nothing",
  hour: 7,
  minute: 0,
  second: 0
};

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS"
  });
  res.end(body);
}

function sendNoContent(res) {
  res.writeHead(204, {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS"
  });
  res.end();
}

function parseJsonBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk.toString();
    });
    req.on("end", () => {
      if (!body) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(body));
      } catch (err) {
        reject(err);
      }
    });
    req.on("error", reject);
  });
}

function validateAlarmShape(alarm, options = { requireId: true }) {
  const { requireId } = options;
  if (!alarm || typeof alarm !== "object") return false;
  if (requireId) {
    if (typeof alarm.id !== "string" || alarm.id.length === 0 || alarm.id.length > 36) return false;
  } else if (alarm.id !== undefined && (typeof alarm.id !== "string" || alarm.id.length > 36)) {
    return false;
  }
  if (!Number.isInteger(alarm.day) || alarm.day < 0 || alarm.day > 6) return false;
  if (!Number.isInteger(alarm.hour) || alarm.hour < 0 || alarm.hour > 23) return false;
  if (!Number.isInteger(alarm.minute) || alarm.minute < 0 || alarm.minute > 59) return false;
  if (!Number.isInteger(alarm.second) || alarm.second < 0 || alarm.second > 59) return false;
  if (typeof alarm.enabled !== "boolean") return false;
  return true;
}

function validateVacationModeShape(mode) {
  if (!mode || typeof mode !== "object") return false;
  if (typeof mode.enabled !== "boolean") return false;
  if (mode.option !== "alarm" && mode.option !== "do_nothing") return false;
  if (!Number.isInteger(mode.hour) || mode.hour < 0 || mode.hour > 23) return false;
  if (!Number.isInteger(mode.minute) || mode.minute < 0 || mode.minute > 59) return false;
  if (!Number.isInteger(mode.second) || mode.second < 0 || mode.second > 59) return false;
  return true;
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url || "/", `http://${req.headers.host}`);
  const path = url.pathname;

  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Headers": "Content-Type",
      "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS"
    });
    res.end();
    return;
  }

  if (req.method === "GET" && path === "/api/alarms") {
    sendJson(res, 200, alarms);
    return;
  }

  if (req.method === "POST" && path === "/api/alarms") {
    try {
      const payload = await parseJsonBody(req);
      if (!validateAlarmShape(payload, { requireId: false })) {
        sendJson(res, 400, { error: "Invalid alarm payload" });
        return;
      }

      const withId = {
        ...payload,
        id: payload.id && payload.id.length > 0 ? payload.id : crypto.randomUUID()
      };
      const idx = alarms.findIndex((a) => a.id === withId.id);
      if (idx >= 0) {
        alarms[idx] = withId;
        sendJson(res, 200, withId);
      } else {
        alarms.push(withId);
        sendJson(res, 201, withId);
      }
    } catch {
      sendJson(res, 400, { error: "Body must be valid JSON" });
    }
    return;
  }

  if (req.method === "GET" && path === "/api/vacation-mode") {
    sendJson(res, 200, vacationMode);
    return;
  }

  if (req.method === "PUT" && path === "/api/vacation-mode") {
    try {
      const payload = await parseJsonBody(req);
      if (!validateVacationModeShape(payload)) {
        sendJson(res, 400, { error: "Invalid vacation mode payload" });
        return;
      }
      vacationMode = payload;
      sendJson(res, 200, vacationMode);
    } catch {
      sendJson(res, 400, { error: "Body must be valid JSON" });
    }
    return;
  }

  if ((req.method === "PUT" || req.method === "DELETE") && path.startsWith("/api/alarms/")) {
    const id = decodeURIComponent(path.replace("/api/alarms/", ""));
    const idx = alarms.findIndex((a) => a.id === id);

    if (idx < 0) {
      sendJson(res, 404, { error: "Alarm not found" });
      return;
    }

    if (req.method === "DELETE") {
      alarms.splice(idx, 1);
      sendNoContent(res);
      return;
    }

    try {
      const payload = await parseJsonBody(req);
      const updated = { ...payload, id };
      if (!validateAlarmShape(updated)) {
        sendJson(res, 400, { error: "Invalid alarm payload" });
        return;
      }
      alarms[idx] = updated;
      sendJson(res, 200, updated);
    } catch {
      sendJson(res, 400, { error: "Body must be valid JSON" });
    }

    return;
  }

  sendJson(res, 404, { error: "Not found" });
});

server.listen(PORT, () => {
  console.log(`Mock alarm API listening on http://localhost:${PORT}`);
});
