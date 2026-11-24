<script setup lang="ts">
import type { Ref } from 'vue';
import type { IListItem, IFiles, TMessage } from '@/types';
import { KEY, CMD } from '@/generated/protocol';

import { watchEffect, ref, computed, nextTick } from 'vue';
import { useDebounceFn } from '@vueuse/core';
import { toByte } from 'vuesp-components/helpers';
import { useFiles } from '@/composables/useFiles';
import { STORAGE_DRAG_MIME, copyStorage, reloadStorages, onStorageReload, type IStorageDrag } from '@/composables/useStorageTransfer';
import { useConnection } from '@/composables/useConnection';
import { useLocale } from '@/composables/useLocale';

import { VIcon, VFile, VCard } from 'vuesp-components/components';

const { $t } = useLocale();

const mainMenu: IListItem<number>[] = [
  { name: $t('upload'), value: 2 },
  { name: $t('reload'), value: 3 },
  { name: $t('format'), value: 4 },
];
const listMenu: IListItem<number>[] = [
  { name: $t('download'), value: 1 },
  { name: $t('remove'), value: 2 },
];

const isLoading = ref(false);
const dragOver = ref(false);
const path = ref(['root']);
const files: Ref<IFiles[]> = ref([]);
const fullPath = computed(() => `${path.value.join('/').replace('root', '')}/`);
const sortFiles = computed<IFiles[]>(() => JSON.parse(JSON.stringify(files.value)).sort((a: IFiles, b: IFiles) => ((a.isFile || 0) > (b.isFile || 0) ? 1 : -1)));

const getListMenu = (isDir: boolean) => listMenu.filter((i) => (isDir ? i.value !== 1 : true));
const isLast = (path: string[], i: number) => path.length > i + 1;

const onMessage = ({ key, object }: TMessage) => {
  if (key === KEY.FILES && object) files.value.push(object);
};

const { onSend, onDialog } = useConnection((send) => {
  send(KEY.FILES, { command: CMD.GET, name: fullPath.value });
}, onMessage);

const onUpdate = () => {
  isLoading.value = true;
  files.value = [];
  onSend(KEY.FILES, { command: CMD.GET, name: fullPath.value });
  onSend(KEY.INFO);
};

const onStorageDragStart = (event: DragEvent, name: string, isDir: boolean) => {
  const drag: IStorageDrag = { source: 'fs', path: fileName(name), name, isDir };
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
  if (drag.source === 'fs') return;

  const ok = await copyStorage(drag, { source: 'fs', dir: fullPath.value });
  if (ok) {
    reloadStorages();
  } else {
    onDialog({ value: true, message: $t('copyError') });
  }
};

onStorageReload(onUpdate);

const { fileName, createLink, onDelete, onUpload, onFormat, onClickUpload } = useFiles(fullPath, onUpdate);

const onPrev = (index: number) => {
  if (path.value.length > index + 1) {
    path.value = path.value.filter((_, i) => i < index + 1);
    nextTick(() => onUpdate());
  }
};

const onNext = (isDir: boolean, value: string) => {
  if (isDir && path) {
    path.value.push(value);
    onUpdate();
  }
};

const onEventService = ({ value }: IListItem<number>) => {
  if (value === 2) onClickUpload();
  if (value === 3) onUpdate();
  if (value === 4) onSureFormat();
};

const onSureFormat = () => onDialog({ value: true, message: $t('dialog.allDel'), callback: onFormat });

const onEventList = (name: string, { value }: IListItem<number>) => {
  if (value === 1) createLink(name);
  if (value === 2) onSureDelete(name);
};

const onSureDelete = (name: string) => {
  if (fileName(name).includes('www')) {
    onDialog({ value: true, message: $t('dialog.wwwDir'), callback: onDelete.bind(this, name) });
  } else {
    onDelete(name);
  }
};

const onLoad = useDebounceFn((_?: IFiles[]) => {
  isLoading.value = false;
}, 300);

watchEffect(() => {
  onLoad(files.value);
});
</script>

<template>
  <div
    class="relative rounded-xl transition-shadow"
    :class="dragOver ? 'ring-2 ring-cyan-400/70' : ''"
    @dragover="onStorageDragOver"
    @dragleave="dragOver = false"
    @drop.prevent="onStorageDrop"
  >
    <VCard :title="$t('fb')" hover>
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

      <VFile multiple="multiple" @change="onUpload"></VFile>

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
