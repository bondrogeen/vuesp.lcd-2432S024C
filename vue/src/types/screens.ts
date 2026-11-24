export type TScreenType = 'status' | 'image' | 'data';

export type TScreenLocation = 'lfs' | 'sd';

export type TScreenAlign = 'left' | 'center' | 'right';

export interface IScreenLabelConfig {
  slot: number;
  x: number;
  y: number;
  w: number;
  h: number;
  size: number;
  color: string;
  align: TScreenAlign;
  text: string;
}

export interface IScreenConfig {
  name: string;
  type: TScreenType;
  location: TScreenLocation;
  file: string;
  bg: string;
  labels: IScreenLabelConfig[];
}

export interface IScreensResponse {
  active: number;
  swipe: number;
  fsFree: number;
  sdFree: number;
  screens: IScreenConfig[];
}

export interface IScreenUploadResult {
  state: boolean;
  file: string;
  location: TScreenLocation;
  size: number;
  name: string;
  bg: string;
}
