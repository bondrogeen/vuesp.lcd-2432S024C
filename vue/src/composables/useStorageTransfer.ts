import { onUnmounted } from 'vue';
import { useFetch } from '@vueuse/core';

export const STORAGE_DRAG_MIME = 'application/x-vuesp-storage';
export const STORAGE_RELOAD_EVENT = 'vuesp:storage-reload';

export interface IStorageDrag {
  source: 'fs' | 'sd';
  path: string;
  name: string;
  isDir: boolean;
}

export const reloadStorages = () => {
  window.dispatchEvent(new Event(STORAGE_RELOAD_EVENT));
};

export const onStorageReload = (callback: () => void) => {
  window.addEventListener(STORAGE_RELOAD_EVENT, callback);
  onUnmounted(() => window.removeEventListener(STORAGE_RELOAD_EVENT, callback));
};

export const copyStorage = async (drag: IStorageDrag, target: { source: 'fs' | 'sd'; dir: string }) => {
  if (drag.source === target.source) return false;

  const query = new URLSearchParams({
    from: drag.source,
    to: target.source,
    file: drag.path,
    dir: target.dir,
  });
  const { data } = await useFetch(`/copy?${query.toString()}`).post().json();
  return Boolean(data.value?.state);
};
