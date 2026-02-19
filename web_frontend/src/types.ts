export type Alarm = {
  id: string;
  day: number;
  hour: number;
  minute: number;
  second: number;
  enabled: boolean;
};

export type AlarmCreateInput = Omit<Alarm, "id">;

export type VacationModeOption = "alarm" | "do_nothing";

export type VacationMode = {
  enabled: boolean;
  option: VacationModeOption;
  hour: number;
  minute: number;
  second: number;
};
