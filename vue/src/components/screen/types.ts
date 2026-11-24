import type { IScreenConfig, IScreenUploadResult } from '@/types';

export interface IScreenPreviewProps {
  screen: IScreenConfig;
}

export interface IScreenUploadDialogProps {
  sdAvailable: boolean;
  defaultName?: string;
}

export interface IScreenUploadDialogEmits {
  close: [];
  saved: [result: IScreenUploadResult];
}
