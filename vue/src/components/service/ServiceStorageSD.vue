<script setup lang="ts">
import type { Ref } from 'vue';
import type { IListItem } from '@/types';

import { computed, onMounted, ref } from 'vue';
import { useFetch } from '@vueuse/core';
import { toByte, createDownloadLink } from 'vuesp-components/helpers';

import { useConnection } from '@/composables/useConnection';
import { STORAGE_DRAG_MIME, copyStorage, reloadStorages, onStorageReload, type IStorageDrag } from '@/composables/useStorageTransfer';
import { useLocale } from '@/composables/useLocale';

import { VIcon, VCard } from 'vuesp-components/components';

interface ISdFile {
  name: string;
  size: number;
  isDir: boolean;
  isFile: boolean;
}

const { $t } = useLocale();
const { onDialog } = useConnection();

const mainMenu: IListItem<number>[] = [
  { name: $t('upload'), value: 2 },
  { name: $t('reload'), value: 3 },
  { name: $t('format'), value: 4 },
];
const listMenu: IListItem<number>[] = [
  { name: $t('download'), value: 1 },
  { name: $t('remove'), value: 2 },
];

const path = ref(['SD']);
const files: Ref<ISdFile[]> = ref([]);
const isLoading = ref(false);
const dragOver = ref(false);
const fileInput = ref<HTMLInputElement | null>(null);

const subPath = computed(() => path.value.slice(1).join('/'));
const fullPath = computed(() => (subPath.value ? `/${subPath.value}` : '/'));
const pathFile = (name: string) => `${fullPath.value === '/' ? '' : fullPath.value}/${name}`;
const sortFiles = computed(() =>
  [...files.value].sort((a, b) => (a.isDir === b.isDir ? a.name.localeCompare(b.name) : a.isDir ? -1 : 1)),
);
const getListMenu = (isDir: boolean) => listMenu.filter((i) => (isDir ? i.value !== 1 : true));
const isLast = (items: string[], i: number) => items.length > i + 1;

const load = async () => {
  isLoading.value = true;
  const { data } = await useFetch(`/sd/list?path=${encodeURIComponent(fullPath.value)}`).get().json();
  files.value = Array.isArray(data.value) ? (data.value as ISdFile[]) : [];
  isLoading.value = false;
};

const onStorageDragStart = (event: DragEvent, name: string, isDir: boolean) => {
  const drag: IStorageDrag = { source: 'sd', path: pathFile(name), name, isDir };
  event.dataTransfer?.setData(STORAGE_DRAG_MIME, JSON.stringify(drag));
  if (event.dataTransfer) event.dataTransfer.effectAllowed = 'copy';
};

const onStorageDragOver = (event: DragEvent) => {
  event.preventDefault();
  if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy';
  dragOver.value = true;
};

const onStorageDrop = async (event: DragEvent) => {
  dragOver.value = false;
  const raw = event.dataTransfer?.getData(STORAGE_DRAG_MIME);
  if (!raw) return;

  let drag: IStorageDrag;
  try {
    drag = JSON.parse(raw) as IStorageDrag;
  } catch {
    return;
  }
  if (drag.source === 'sd') return;

  const ok = await copyStorage(drag, { source: 'sd', dir: fullPath.value });
  if (ok) {
    reloadStorages();
  } else {
    onDialog({ value: true, message: $t('copyError') });
  }
};

const onNext = (isDir: boolean, name: string) => {
  if (!isDir) return;
  path.value.push(name);
  load();
};

const onPrev = (index: number) => {
  if (path.value.length > index + 1) {
    path.value = path.value.filter((_, i) => i < index + 1);
    load();
  }
};

const onFileInput = async (event: Event) => {
  const input = event.target as HTMLInputElement;
  const list = input.files;
  if (!list || !list.length) return;

  const body = new FormData();
  for (let i = 0; i < list.length; i++) {
    const file = list.item(i);
    if (file) body.append(`file[${i}]`, file, pathFile(file.name));
  }
  const { data } = await useFetch('/sd', { body }).post().json();
  if (data.value?.state) await load();
  input.value = '';
};

const onDelete = async (name: string) => {
  const { data } = await useFetch(`/sd?file=${encodeURIComponent(pathFile(name))}`).delete().json();
  if (data.value?.state) await load();
};

const onSureDelete = (name: string) => {
  onDialog({ value: true, message: $t('sdDelete', { name }), callback: () => onDelete(name) });
};

const onDownload = (name: string) => {
  createDownloadLink(`/sd?file=${encodeURIComponent(pathFile(name))}`, name);
};

const onFormat = async () => {
  isLoading.value = true;
  const { data } = await useFetch('/sd?format=true').post().json();
  isLoading.value = false;
  if (data.value?.state) {
    path.value = ['SD'];
    await load();
  } else {
    onDialog({ value: true, message: $t('sdFormatError') });
  }
};

const onSureFormat = () => {
  onDialog({ value: true, message: $t('sdFormat'), callback: onFormat });
};

const onEventService = ({ value }: IListItem<number>) => {
  if (value === 2) fileInput.value?.click();
  if (value === 3) load();
  if (value === 4) onSureFormat();
};

const onEventList = (name: string, { value }: IListItem<number>) => {
  if (value === 1) onDownload(name);
  if (value === 2) onSureDelete(name);
};

onMounted(load);
onStorageReload(load);
</script>

<template>
  <div
    class="relative rounded-xl transition-shadow"
    :class="dragOver ? 'ring-2 ring-cyan-400/70' : ''"
    @dragover="onStorageDragOver"
    @dragleave="dragOver = false"
    @drop.prevent="onStorageDrop"
  >
    <VCard :title="$t('sdCard')" hover>
    <div class="flex items-center gap-2 px-4 pb-2">
      <div class="flex min-w-0 flex-auto items-center gap-2 font-bold">
        <div
          v-for="(value, i) of path"
          :key="value"
          class="flex max-w-40 cursor-pointer items-center gap-2 truncate text-gray-dark-25 last:cursor-default last:text-gray-dark-40 dark:text-gray-light-10 dark:last:text-white"
          :title="value"
          @click="onPrev(i)"
        >
          <div>{{ value }}</div>

          <VIcon name="arrow-right" v-if="isLast(path, i)" class="h-5 w-4"></VIcon>
        </div>
      </div>

      <input ref="fileInput" type="file" multiple class="hidden" @change="onFileInput" />

      <v-select :items="mainMenu" @change="onEventService">
        <template #activator="{ on }">
          <v-button type="icon" variant="ghost" @click="on.click">
            <VIcon name="more" class="size-5 rotate-90"></VIcon>
          </v-button>
        </template>
      </v-select>
    </div>

    <div class="relative min-h-[260px]">
      <v-loader v-if="isLoading" class="absolute left-1/2 top-1/2 text-cyan-main" />

      <v-list v-slot="{ item: { name, size, isDir, isFile } }" :items="sortFiles">
        <div
          class="flex min-w-0 flex-1 items-center"
          draggable="true"
          @click="onNext(Boolean(isDir), name)"
          @dragstart="onStorageDragStart($event, name, Boolean(isDir))"
        >
          <div class="mr-4 text-gray-main">
            <VIcon name="folder" v-if="isDir" class="size-4"></VIcon>
            <VIcon name="file" v-else class="size-4"></VIcon>
          </div>

          <div class="min-w-0">
            <div class="truncate text-sm" :title="name">{{ name }}</div>

            <div v-if="isFile" class="truncate text-xs text-gray-main">{{ toByte(size) }} ({{ size }})</div>
          </div>
        </div>

        <div class="shrink-0">
          <v-select :items="getListMenu(Boolean(isDir))" @change="onEventList(name, $event)">
            <template #activator="{ on }">
              <v-button @click="on.click" variant="ghost">
                <VIcon name="more" class="size-5 rotate-90"></VIcon>
              </v-button>
            </template>
          </v-select>
        </div>
      </v-list>
    </div>
  </VCard>
  </div>
</template>
