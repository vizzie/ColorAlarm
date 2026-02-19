import m from "mithril";
import * as C from "construct-ui";
import {
  createAlarm,
  deleteAlarm,
  getVacationMode,
  listAlarms,
  updateAlarm,
  updateVacationMode
} from "./api";
import { DAY_OPTIONS } from "./dayOptions";
import type { Alarm, VacationMode } from "./types";

const UI = ((C as any).default ?? C) as any;

type AlarmForm = {
  day: number;
  hour: number;
  minute: number;
  enabled: boolean;
};

function emptyForm(): AlarmForm {
  return {
    day: 1,
    hour: 6,
    minute: 30,
    enabled: true
  };
}

function toAlarmPayload(form: AlarmForm) {
  return {
    day: form.day,
    hour: form.hour,
    minute: form.minute,
    second: 0,
    enabled: form.enabled
  };
}

export const App: m.Component = {
  oninit: async () => {
    await state.loadAll();
  },
  view: () => {
    const selectedDayLabel = DAY_OPTIONS.find((d) => d.value === state.form.day)?.label ?? "-";

    return m("main.page", [
      m("section.hero", [
        m("h1", "ColorAlarm"),
        m("p", "A Color Clock inside an IKEA lamp!")
      ]),

      state.error ? m("div.error", state.error) : null,

      m("section.panel", [
        m("h2", "Vacation Mode"),
        m("div.form-grid", [
          m("label", "Enabled"),
          m(UI.Switch, {
            checked: state.vacationMode.enabled,
            label: state.vacationMode.enabled ? "On" : "Off",
            onchange: () => {
              state.vacationMode = {
                ...state.vacationMode,
                enabled: !state.vacationMode.enabled
              };
            }
          }),

          state.vacationMode.enabled ? m("label", "Mode") : null,
          state.vacationMode.enabled
            ? m(
                "select.input",
                {
                  value: state.vacationMode.option,
                  onchange: (e: Event) => {
                    const target = e.target as HTMLSelectElement;
                    state.vacationMode = {
                      ...state.vacationMode,
                      option: target.value as VacationMode["option"]
                    };
                  }
                },
                [
                  m("option", { value: "alarm" }, "Alarm"),
                  m("option", { value: "do_nothing" }, "Do Nothing")
                ]
              )
            : null,

          state.vacationMode.enabled && state.vacationMode.option === "alarm"
            ? m("label", "Alarm Time")
            : null,
          state.vacationMode.enabled && state.vacationMode.option === "alarm"
            ? m("input.input", {
                type: "time",
                step: 60,
                value: formatTimeInput(state.vacationMode.hour, state.vacationMode.minute),
                oninput: (e: Event) => {
                  const target = e.target as HTMLInputElement;
                  const parsed = parseTimeInput(target.value);
                  if (!parsed) {
                    return;
                  }
                  state.vacationMode = {
                    ...state.vacationMode,
                    hour: parsed.hour,
                    minute: parsed.minute,
                    second: 0
                  };
                }
              })
            : null
        ]),

        m("div.actions", [
          m(UI.Button, {
            intent: UI.Intent?.PRIMARY,
            disabled: state.vacationBusy,
            label: "Save Vacation Mode",
            onclick: async () => {
              await state.saveVacationMode();
            }
          })
        ])
      ]),

      m("section.panel", [
        m("h2", state.editingId ? `Edit Alarm: ${state.editingId}` : "Create Alarm"),
        m("div.form-grid", [
          m("label", "Day"),
          m(
            "select.input",
            {
              value: String(state.form.day),
              onchange: (e: Event) => {
                const target = e.target as HTMLSelectElement;
                state.form.day = Number(target.value);
              }
            },
            DAY_OPTIONS.map((option) => m("option", { value: String(option.value) }, option.label))
          ),

          m("label", "Alarm Time"),
          m("input.input", {
            type: "time",
            step: 60,
            value: formatTimeInput(state.form.hour, state.form.minute),
            oninput: (e: Event) => {
              const target = e.target as HTMLInputElement;
              const parsed = parseTimeInput(target.value);
              if (!parsed) {
                return;
              }
              state.form.hour = parsed.hour;
              state.form.minute = parsed.minute;
            }
          }),

          m("label", "Enabled"),
          m(UI.Switch, {
            checked: state.form.enabled,
            label: state.form.enabled ? "Enabled" : "Disabled",
            onchange: () => {
              state.form.enabled = !state.form.enabled;
            }
          })
        ]),

        m("p.meta", `Preview: ${selectedDayLabel} ${formatTimeInput(state.form.hour, state.form.minute)}`),

        m("div.actions", [
          m(UI.Button, {
            intent: UI.Intent?.PRIMARY,
            disabled: state.busy,
            label: state.editingId ? "Save Changes" : "Create Alarm",
            onclick: async () => {
              await state.submit();
            }
          }),
          state.editingId
            ? m(UI.Button, {
                basic: true,
                disabled: state.busy,
                label: "Cancel",
                onclick: () => {
                  state.editingId = null;
                  state.form = emptyForm();
                }
              })
            : null
        ])
      ]),

      m("section.panel", [
        m("h2", "Existing Alarms"),
        state.busy && state.alarms.length === 0 ? m("p", "Loading...") : null,
        state.alarms.length === 0
          ? m("p", "No alarms configured.")
          : m("table.table", [
              m("thead", [
                m("tr", [
                  m("th", "ID"),
                  m("th", "Day"),
                  m("th", "Time"),
                  m("th", "Enabled"),
                  m("th", "Actions")
                ])
              ]),
              m(
                "tbody",
                state.alarms.map((alarm) =>
                  m("tr", { key: alarm.id }, [
                    m("td", alarm.id),
                    m("td", DAY_OPTIONS.find((d) => d.value === alarm.day)?.label ?? String(alarm.day)),
                    m("td", formatTimeInput(alarm.hour, alarm.minute)),
                    m("td", alarm.enabled ? "Yes" : "No"),
                    m("td.row-actions", [
                      m(UI.Button, {
                        size: UI.Size?.SM ?? "sm",
                        label: "Edit",
                        onclick: () => {
                          state.editingId = alarm.id;
                          state.form = {
                            day: alarm.day,
                            hour: alarm.hour,
                            minute: alarm.minute,
                            enabled: alarm.enabled
                          };
                        }
                      }),
                      m(UI.Button, {
                        size: UI.Size?.SM ?? "sm",
                        intent: UI.Intent?.NEGATIVE ?? "negative",
                        label: "Delete",
                        onclick: async () => {
                          await state.remove(alarm.id);
                        }
                      })
                    ])
                  ])
                )
              )
            ])
      ])
    ]);
  }
};

const state = {
  alarms: [] as Alarm[],
  form: emptyForm(),
  editingId: null as string | null,
  vacationMode: {
    enabled: false,
    option: "do_nothing",
    hour: 7,
    minute: 0,
    second: 0
  } as VacationMode,
  busy: false,
  vacationBusy: false,
  error: "" as string,

  async loadAll() {
    await Promise.all([state.load(), state.loadVacationMode()]);
  },

  async load() {
    state.busy = true;
    state.error = "";
    try {
      state.alarms = await listAlarms();
    } catch (err) {
      state.error = err instanceof Error ? err.message : "Failed to load alarms";
    } finally {
      state.busy = false;
      m.redraw();
    }
  },

  async loadVacationMode() {
    state.vacationBusy = true;
    state.error = "";
    try {
      state.vacationMode = await getVacationMode();
    } catch (err) {
      state.error = err instanceof Error ? err.message : "Failed to load vacation mode";
    } finally {
      state.vacationBusy = false;
      m.redraw();
    }
  },

  async submit() {
    state.busy = true;
    state.error = "";

    const alarmPayload = toAlarmPayload(state.form);

    try {
      if (state.editingId) {
        await updateAlarm({ id: state.editingId, ...alarmPayload });
      } else {
        await createAlarm(alarmPayload);
      }
      state.form = emptyForm();
      state.editingId = null;
      await state.load();
    } catch (err) {
      state.error = err instanceof Error ? err.message : "Failed to save alarm";
    } finally {
      state.busy = false;
      m.redraw();
    }
  },

  async remove(id: string) {
    state.busy = true;
    state.error = "";
    try {
      await deleteAlarm(id);
      if (state.editingId === id) {
        state.editingId = null;
        state.form = emptyForm();
      }
      await state.load();
    } catch (err) {
      state.error = err instanceof Error ? err.message : "Failed to delete alarm";
    } finally {
      state.busy = false;
      m.redraw();
    }
  },

  async saveVacationMode() {
    state.vacationBusy = true;
    state.error = "";
    try {
      const normalizedMode: VacationMode = {
        ...state.vacationMode,
        second: 0
      };
      state.vacationMode = await updateVacationMode(normalizedMode);
    } catch (err) {
      state.error = err instanceof Error ? err.message : "Failed to save vacation mode";
    } finally {
      state.vacationBusy = false;
      m.redraw();
    }
  }
};

function pad(value: number): string {
  return String(value).padStart(2, "0");
}

function formatTimeInput(hour: number, minute: number): string {
  return `${pad(hour)}:${pad(minute)}`;
}

function parseTimeInput(value: string): { hour: number; minute: number } | null {
  const parts = value.split(":");
  if (parts.length !== 2) {
    return null;
  }

  const hour = Number(parts[0]);
  const minute = Number(parts[1]);

  if (!Number.isInteger(hour) || !Number.isInteger(minute)) {
    return null;
  }
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return null;
  }

  return { hour, minute };
}
