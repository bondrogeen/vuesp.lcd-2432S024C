<script setup lang="ts">
import { computed, onMounted, shallowRef, useTemplateRef, watch } from 'vue';
import { useLocale } from '@/composables/useLocale';

import type { IScreenUploadResult, TScreenLocation } from '@/types';
import type { IScreenUploadDialogEmits, IScreenUploadDialogProps } from './types';

import { VButton, VDialog, VIcon, VTextField } from 'vuesp-components/components';

const { sdAvailable, defaultName = 'screen-1' } = defineProps<IScreenUploadDialogProps>();
const emit = defineEmits<IScreenUploadDialogEmits>();

const { $t } = useLocale();

const canvas = useTemplateRef('canvas');
const image = shallowRef<HTMLImageElement | null>(null);
const name = shallowRef(defaultName);
const location = shallowRef<TScreenLocation>('lfs');
const background = shallowRef('#000000');
const scalePct = shallowRef(100);
const offsetX = shallowRef(0);
const offsetY = shallowRef(0);
const rotation = shallowRef(0);
const busy = shallowRef(false);
const error = shallowRef('');

const hasImage = computed(() => image.value !== null);
const fileLabel = computed(() => name.value.trim() || $t('screens.image'));

const draw = () => {
  const element = canvas.value;
  if (!element) return;

  const context = element.getContext('2d');
  if (!context) return;

  context.setTransform(1, 0, 0, 1, 0, 0);
  context.fillStyle = background.value;
  context.fillRect(0, 0, element.width, element.height);

  const source = image.value;
  if (!source) return;

  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.translate(element.width / 2 + offsetX.value, element.height / 2 + offsetY.value);
  context.rotate((rotation.value * Math.PI) / 180);
  context.scale(scalePct.value / 100, scalePct.value / 100);
  context.drawImage(source, -source.naturalWidth / 2, -source.naturalHeight / 2);
};

watch([scalePct, offsetX, offsetY, rotation, background], draw);

onMounted(draw);

const fit = (mode: 'contain' | 'cover') => {
  if (!image.value) return;
  const swap = rotation.value % 180 !== 0;
  const width = swap ? image.value.naturalHeight : image.value.naturalWidth;
  const height = swap ? image.value.naturalWidth : image.value.naturalHeight;
  if (!width || !height) return;

  const contain = Math.min(320 / width, 240 / height);
  const cover = Math.max(320 / width, 240 / height);
  scalePct.value = Math.round((mode === 'cover' ? cover : contain) * 100);
  offsetX.value = 0;
  offsetY.value = 0;
};

const onFile = (event: Event) => {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) return;

  error.value = '';
  const url = URL.createObjectURL(file);
  const loaded = new Image();
  loaded.onload = () => {
    image.value = loaded;
    rotation.value = 0;
    fit('cover');
    draw();
    URL.revokeObjectURL(url);
  };
  loaded.onerror = () => {
    error.value = $t('screens.uploadError');
    URL.revokeObjectURL(url);
  };
  loaded.src = url;
};

let drag: { x: number; y: number; offsetX: number; offsetY: number } | null = null;

const onPointerDown = (event: PointerEvent) => {
  const element = canvas.value;
  if (!element || !image.value) return;
  element.setPointerCapture(event.pointerId);
  drag = { x: event.clientX, y: event.clientY, offsetX: offsetX.value, offsetY: offsetY.value };
};

const onPointerMove = (event: PointerEvent) => {
  const element = canvas.value;
  if (!element || !drag) return;
  const rect = element.getBoundingClientRect();
  const ratio = rect.width ? 320 / rect.width : 1;
  offsetX.value = drag.offsetX + (event.clientX - drag.x) * ratio;
  offsetY.value = drag.offsetY + (event.clientY - drag.y) * ratio;
};

const onPointerUp = () => {
  drag = null;
};

const onWheel = (event: WheelEvent) => {
  const next = scalePct.value + (event.deltaY < 0 ? 10 : -10);
  scalePct.value = Math.min(400, Math.max(10, next));
};

const rotate = () => {
  rotation.value = (rotation.value + 90) % 360;
  draw();
};

const resetView = () => {
  rotation.value = 0;
  scalePct.value = 100;
  offsetX.value = 0;
  offsetY.value = 0;
  draw();
};

const fileBase = () => {
  const cleaned = name.value
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, '-')
    .replace(/^-+|-+$/g, '');
  return cleaned || 'screen';
};

const upload = async () => {
  if (!image.value || busy.value) return;
  const element = canvas.value;
  if (!element) return;

  busy.value = true;
  error.value = '';

  try {
    const blob = await new Promise<Blob | null>((resolve) => {
      element.toBlob((value) => resolve(value), 'image/jpeg', 0.85);
    });
    if (!blob) throw new Error('blob');

    const base = fileBase();
    const body = new FormData();
    body.append('file', blob, `${base}.jpg`);

    const response = await fetch(
      `/screens/image?name=${encodeURIComponent(base)}&location=${location.value}`,
      { method: 'POST', body },
    );
    const data = (await response.json()) as IScreenUploadResult;
    if (!response.ok || !data.state) throw new Error('upload');

    emit('saved', {
      ...data,
      name: name.value.trim() || base,
      bg: background.value,
    });
  } catch (cause) {
    console.warn(cause);
    error.value = $t('screens.uploadError');
  } finally {
    busy.value = false;
  }
};
</script>

<template>
  <VDialog size="lg" :title="$t('screens.uploadTitle')" @close="emit('close')">
    <div class="flex flex-col gap-4 lg:flex-row">
      <div class="flex min-w-0 flex-1 flex-col gap-3">
        <div class="relative overflow-hidden rounded-lg border border-gray-light-10 bg-black dark:border-gray-dark-30">
          <canvas
            ref="canvas"
            width="320"
            height="240"
            class="block w-full cursor-move touch-none"
            @pointerdown="onPointerDown"
            @pointermove="onPointerMove"
            @pointerup="onPointerUp"
            @pointercancel="onPointerUp"
            @wheel.prevent="onWheel"
          ></canvas>
          <div v-if="!hasImage" class="pointer-events-none absolute inset-0 flex items-center justify-center px-4 text-center text-sm text-white/60">
            {{ $t('screens.chooseFile') }}
          </div>
        </div>

        <div class="flex flex-wrap items-center gap-2">
          <label class="inline-flex cursor-pointer items-center gap-2 rounded-md border border-gray-light-10 p-2 dark:border-gray-dark-30" :title="$t('screens.chooseFile')" :aria-label="$t('screens.chooseFile')">
            <VIcon name="upload" :size="18" />
            <input type="file" accept="image/*" class="hidden" @change="onFile" />
          </label>
          <VButton size="sm" variant="outline" color="gray" :disabled="!hasImage" @click="fit('contain')">{{ $t('screens.fit') }}</VButton>
          <VButton size="sm" variant="outline" color="gray" :disabled="!hasImage" @click="fit('cover')">{{ $t('screens.cover') }}</VButton>
          <VButton size="sm" variant="outline" color="gray" :disabled="!hasImage" @click="rotate">{{ $t('screens.rotate') }}</VButton>
          <VButton size="sm" variant="ghost" color="gray" :disabled="!hasImage" @click="resetView">{{ $t('screens.resetView') }}</VButton>
        </div>

        <div>
          <div class="flex items-center justify-between text-xs text-gray-dark-25 dark:text-gray-light-10">
            <span>{{ $t('screens.zoom') }}</span>
            <span class="font-mono">{{ scalePct }}%</span>
          </div>
          <input v-model.number="scalePct" type="range" min="10" max="400" class="mt-1 w-full accent-cyan-500" :disabled="!hasImage" />
        </div>

        <p class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.uploadHint') }}</p>
      </div>

      <div class="flex w-full flex-col gap-3 lg:w-64">
        <VTextField v-model="name" :label="$t('screens.name')" />

        <div>
          <div class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.location') }}</div>
          <div class="mt-1 flex gap-2">
            <VButton size="sm" :variant="location === 'lfs' ? 'solid' : 'outline'" color="blue" @click="location = 'lfs'">{{ $t('screens.lfs') }}</VButton>
            <VButton size="sm" :variant="location === 'sd' ? 'solid' : 'outline'" color="blue" :disabled="!sdAvailable" @click="location = 'sd'">{{ $t('screens.sd') }}</VButton>
          </div>
        </div>

        <div>
          <div class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.background') }}</div>
          <input v-model="background" type="color" class="mt-1 h-9 w-full cursor-pointer rounded-md border border-gray-light-10 bg-transparent p-1 dark:border-gray-dark-30" />
        </div>

        <div class="mt-auto flex flex-col gap-2">
          <p v-if="error" class="text-xs text-red-500">{{ error }}</p>
          <VButton color="blue" block :title="$t('screens.upload')" :aria-label="$t('screens.upload')" :loading="busy" :disabled="!hasImage" @click="upload">
            <VIcon name="upload" :size="18" />
          </VButton>
          <VButton variant="ghost" color="gray" block :title="$t('screens.cancel')" :aria-label="$t('screens.cancel')" @click="emit('close')">
            <VIcon name="close" :size="18" />
          </VButton>
          <p class="truncate text-center text-xs text-gray-dark-25 dark:text-gray-light-10">{{ fileLabel }}.jpg</p>
        </div>
      </div>
    </div>
  </VDialog>
</template>
