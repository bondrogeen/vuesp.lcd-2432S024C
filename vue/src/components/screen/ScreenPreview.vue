<script setup lang="ts">
import { computed } from 'vue';

import type { IScreenLabelConfig } from '@/types';
import type { IScreenPreviewProps } from './types';

import { useLocale } from '@/composables/useLocale';

const { $t } = useLocale();
const { screen } = defineProps<IScreenPreviewProps>();

const imageUrl = computed(() =>
  screen.file
    ? `/screens/image?location=${screen.location}&file=${encodeURIComponent(screen.file)}`
    : '',
);

const labelStyle = (label: IScreenLabelConfig) => ({
  left: `${(label.x / 320) * 100}%`,
  top: `${(label.y / 240) * 100}%`,
  width: `${(label.w / 320) * 100}%`,
  height: `${(label.h / 240) * 100}%`,
  color: label.color,
  backgroundColor: screen.bg,
  fontSize: `${label.size * 2.5}cqw`,
  lineHeight: `${label.size * 2.5}cqw`,
  textAlign: label.align,
});
</script>

<template>
  <div
    class="@container relative aspect-[4/3] w-full overflow-hidden rounded-lg border border-gray-light-10 bg-black dark:border-gray-dark-30"
  >
    <img v-if="screen.type === 'image' && imageUrl" :src="imageUrl" alt="" class="absolute inset-0 size-full object-cover" />

    <div v-else-if="screen.type === 'status'" class="absolute inset-0 flex flex-col" :style="{ backgroundColor: screen.bg }">
      <div class="flex h-[14%] items-center justify-between bg-[#000080] px-2 text-white" :style="{ fontSize: '2.6cqw' }">
        <span class="font-semibold">VUESP LCD</span>
        <span>5.0.0</span>
      </div>
      <div class="grid flex-1 grid-cols-2 gap-x-2 px-2 py-1 text-[#d1d5db]" :style="{ fontSize: '2.1cqw' }">
        <span>Wi-Fi: vuesp</span>
        <span>Touch: idle</span>
        <span>IP: 192.168.1.50</span>
        <span>Raw: 120,340</span>
        <span>RSSI: -61 dBm</span>
        <span>Count: 12</span>
        <span>Uptime: 0:12:40</span>
        <span>Driver: ILI9341</span>
        <span>Heap: 180 KB</span>
        <span>Size: 320x240</span>
      </div>
      <div class="border-t border-[#4b5563] px-2 py-1 text-[#9ca3af]" :style="{ fontSize: '2cqw' }">
        Swipe to change screens
      </div>
    </div>

    <div v-else class="absolute inset-0" :style="{ backgroundColor: screen.bg }"></div>

    <div
      v-for="(label, index) in screen.labels"
      :key="index"
      class="absolute overflow-hidden whitespace-nowrap font-mono"
      :style="labelStyle(label)"
    >
      {{ label.text }}
    </div>

    <div v-if="screen.type === 'image' && !imageUrl" class="absolute inset-0 flex items-center justify-center text-sm text-white/70">
      {{ $t('screens.emptyImage') }}
    </div>
  </div>
</template>
