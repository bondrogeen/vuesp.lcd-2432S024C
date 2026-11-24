<script setup lang="ts">
import { computed, nextTick, onMounted, ref, watch } from 'vue';
import { useLocale } from '@/composables/useLocale';

import type { IScreenConfig, IScreenUploadResult, IScreensResponse, TScreenAlign } from '@/types';

import { VButton, VCard, VIcon } from 'vuesp-components/components';

import ScreenPreview from '@/components/screen/ScreenPreview.vue';
import ScreenUploadDialog from '@/components/screen/ScreenUploadDialog.vue';

const { $t } = useLocale();

const screens = ref<IScreenConfig[]>([]);
const active = ref(0);
const swipe = ref(true);
const fsFree = ref(0);
const sdFree = ref(0);
const loading = ref(true);
const saving = ref(false);
const dirty = ref(false);
const uploadOpen = ref(false);
const error = ref('');

const maxScreens = 8;
const maxLabels = 8;

const canAdd = computed(() => screens.value.length < maxScreens);
const sdAvailable = computed(() => sdFree.value > 0);

const formatSize = (bytes: number) => `${(bytes / 1024 / 1024).toFixed(2)} MB`;

const load = async () => {
  loading.value = true;
  error.value = '';
  try {
    const response = await fetch('/screens');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = (await response.json()) as IScreensResponse;
    screens.value = data.screens;
    active.value = data.active;
    swipe.value = data.swipe === 1;
    fsFree.value = data.fsFree;
    sdFree.value = data.sdFree;
  } catch (cause) {
    console.warn(cause);
    error.value = $t('screens.loadError');
  } finally {
    await nextTick();
    loading.value = false;
    dirty.value = false;
  }
};

watch(
  screens,
  () => {
    if (!loading.value) dirty.value = true;
  },
  { deep: true },
);

const sanitize = (value: unknown) => String(value ?? '').replace(/[|\r\n]/g, ' ').trim();

const toConfig = () => {
  const lines = ['V1'];
  screens.value.forEach((screen, index) => {
    if (screen.type === 'image') {
      lines.push(`screen=${index}|${sanitize(screen.name)}|image|${screen.location}|${sanitize(screen.file)}|${screen.bg}`);
    } else if (screen.type === 'data') {
      lines.push(`screen=${index}|${sanitize(screen.name)}|data|${screen.bg}`);
    } else {
      lines.push(`screen=${index}|${sanitize(screen.name)}|status`);
    }

    screen.labels.forEach((label) => {
      lines.push(
        `label=${index}|${label.slot}|${label.x}|${label.y}|${label.w}|${label.h}|${label.size}|${label.color}|${label.align}|${sanitize(label.text)}`,
      );
    });
  });
  return `${lines.join('\n')}\n`;
};

const save = async () => {
  if (saving.value) return;
  saving.value = true;
  error.value = '';
  try {
    const body = new FormData();
    body.append('config', new Blob([toConfig()], { type: 'text/plain' }), 'screens.cfg');
    const response = await fetch('/screens', { method: 'POST', body });
    const data = (await response.json()) as { state: boolean };
    if (!response.ok || !data.state) throw new Error('save');
    dirty.value = false;
  } catch (cause) {
    console.warn(cause);
    error.value = $t('screens.saveError');
  } finally {
    saving.value = false;
  }
};

const activate = async (index: number) => {
  try {
    const response = await fetch(`/screens/activate?index=${index}`, { method: 'POST' });
    const data = (await response.json()) as { state: boolean };
    if (data.state) active.value = index;
  } catch (cause) {
    console.warn(cause);
  }
};

const toggleSwipe = async () => {
  const next = !swipe.value;
  try {
    await fetch(`/screens/state?swipe=${next ? 1 : 0}`, { method: 'POST' });
    swipe.value = next;
  } catch (cause) {
    console.warn(cause);
  }
};

const addData = async () => {
  if (!canAdd.value) return;
  screens.value.push({
    name: `Screen ${screens.value.length + 1}`,
    type: 'data',
    location: 'lfs',
    file: '',
    bg: '#000000',
    labels: [
      {
        slot: 0,
        x: 8,
        y: 16,
        w: 304,
        h: 32,
        size: 4,
        color: '#00FF88',
        align: 'left',
        text: 'Value: {u0}',
      },
    ],
  });
  await save();
};

const removeScreen = async (index: number) => {
  if (!window.confirm($t('screens.removeConfirm'))) return;
  const removed = screens.value[index];
  screens.value.splice(index, 1);
  if (active.value >= screens.value.length) active.value = Math.max(0, screens.value.length - 1);

  if (removed?.type === 'image' && removed.file) {
    const used = screens.value.some((screen) => screen.location === removed.location && screen.file === removed.file);
    if (!used) {
      await fetch(`/screens/image?location=${removed.location}&file=${encodeURIComponent(removed.file)}`, { method: 'DELETE' });
    }
  }

  await save();
};

const move = async (index: number, direction: number) => {
  const next = index + direction;
  if (next < 0 || next >= screens.value.length) return;
  const [item] = screens.value.splice(index, 1);
  screens.value.splice(next, 0, item);
  await save();
};

const removeLabel = (screen: IScreenConfig, index: number) => {
  screen.labels.splice(index, 1);
};

const addLabel = (screen: IScreenConfig) => {
  if (screen.labels.length >= maxLabels) return;
  const used = new Set(screen.labels.map((label) => label.slot));
  let slot = 0;
  while (used.has(slot) && slot < maxLabels) slot++;

  screen.labels.push({
    slot,
    x: 8,
    y: 8 + screen.labels.length * 40,
    w: 304,
    h: 24,
    size: 2,
    color: '#FFFFFF',
    align: 'left',
    text: 'Value: {u0}',
  });
};

const onSaved = async (result: IScreenUploadResult) => {
  uploadOpen.value = false;
  if (!canAdd.value) return;
  if (!result.file || !result.location) {
    error.value = $t('screens.uploadError');
    return;
  }

  screens.value.push({
    name: result.name,
    type: 'image',
    location: result.location,
    file: result.file,
    bg: result.bg,
    labels: [],
  });
  await save();
};

const reset = async () => {
  if (!window.confirm($t('screens.resetConfirm'))) return;
  try {
    await fetch('/screens/reset', { method: 'POST' });
    window.setTimeout(load, 400);
  } catch (cause) {
    console.warn(cause);
  }
};

const cyrillic = (value: string) => /[\u0400-\u04FF]/.test(value);
const hasCyrillic = computed(() => screens.value.some((screen) => screen.labels.some((label) => cyrillic(label.text))));

const alignOptions: TScreenAlign[] = ['left', 'center', 'right'];

onMounted(load);
</script>

<template>
  <div class="flex flex-col gap-4">
    <VCard variant="glass" padding="lg">
      <div class="flex flex-wrap items-start justify-between gap-4">
        <div>
          <h3 class="text-base font-semibold">{{ $t('screens.title') }}</h3>
          <p class="mt-1 text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.subtitle') }}</p>
        </div>

        <div class="flex flex-wrap gap-2">
          <VButton color="blue" :title="$t('screens.addImageTitle')" :aria-label="$t('screens.addImageTitle')" :disabled="!canAdd" @click="uploadOpen = true">
            <VIcon name="upload" :size="18" />
          </VButton>
          <VButton variant="outline" color="gray" :title="$t('screens.addDataTitle')" :aria-label="$t('screens.addDataTitle')" :disabled="!canAdd" @click="addData">
            <VIcon name="add" :size="18" />
          </VButton>
          <VButton color="green" :title="$t('screens.save')" :aria-label="$t('screens.save')" :loading="saving" :disabled="!dirty" @click="save">
            <VIcon name="save" :size="18" />
          </VButton>
        </div>
      </div>

      <div class="mt-4 grid grid-cols-2 gap-3 text-sm sm:grid-cols-3">
        <div>
          <div class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.active') }}</div>
          <div class="mt-1 truncate font-medium">{{ screens[active]?.name || '—' }}</div>
        </div>
        <div>
          <div class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.count') }}</div>
          <div class="mt-1 font-mono font-medium">{{ screens.length }} / {{ maxScreens }}</div>
        </div>
        <div>
          <div class="text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.freeFs') }}</div>
          <div class="mt-1 font-mono font-medium">{{ formatSize(fsFree) }}</div>
        </div>
      </div>

      <div class="mt-4 flex flex-wrap items-center gap-2">
        <VButton size="sm" :variant="swipe ? 'solid' : 'outline'" :color="swipe ? 'green' : 'gray'" @click="toggleSwipe">
          {{ $t('screens.swipe') }}: {{ swipe ? $t('screens.on') : $t('screens.off') }}
        </VButton>
        <VButton size="sm" variant="ghost" color="gray" :title="$t('screens.reset')" :aria-label="$t('screens.reset')" @click="reset">
          <VIcon name="refresh" :size="16" />
        </VButton>
        <span v-if="dirty" class="text-xs text-amber-600 dark:text-amber-400">{{ $t('screens.dirty') }}</span>
      </div>

      <p v-if="error" class="mt-3 text-xs text-red-500">{{ error }}</p>
      <p v-if="hasCyrillic" class="mt-2 text-xs text-amber-600 dark:text-amber-400">{{ $t('screens.cyrillic') }}</p>
    </VCard>

    <VCard v-if="loading" variant="glass" padding="lg">
      <p class="text-sm text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.loading') }}</p>
    </VCard>

    <div v-else-if="screens.length" class="grid grid-cols-1 items-start gap-4 xl:grid-cols-2">
      <VCard v-for="(screen, index) in screens" :key="index" variant="glass" padding="lg">
        <div class="flex flex-wrap items-center gap-2">
          <input
            v-model="screen.name"
            maxlength="19"
            :aria-label="$t('screens.name')"
            class="min-w-0 flex-1 rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm font-semibold dark:border-gray-dark-30"
          />
          <span
            class="rounded-full px-2 py-1 text-xs font-semibold"
            :class="screen.type === 'status' ? 'bg-slate-500/10 text-slate-500' : screen.type === 'image' ? 'bg-cyan-500/10 text-cyan-600 dark:text-cyan-400' : 'bg-emerald-500/10 text-emerald-600 dark:text-emerald-400'"
          >
            {{ $t(`screens.type.${screen.type}`) }}
          </span>
          <span v-if="active === index" class="rounded-full bg-blue-500/10 px-2 py-1 text-xs font-semibold text-blue-600 dark:text-blue-400">
            {{ $t('screens.active') }}
          </span>
        </div>

        <div class="mt-3 flex flex-wrap items-center gap-1">
          <VButton type="icon" variant="ghost" color="blue" :title="$t('screens.show')" :aria-label="$t('screens.show')" @click="activate(index)">
            <VIcon name="eye" :size="18" />
          </VButton>
          <VButton type="icon" variant="ghost" color="gray" :title="$t('screens.up')" :aria-label="$t('screens.up')" :disabled="index === 0" @click="move(index, -1)">
            <VIcon name="chevron-down" :size="18" class="rotate-180" />
          </VButton>
          <VButton type="icon" variant="ghost" color="gray" :title="$t('screens.down')" :aria-label="$t('screens.down')" :disabled="index === screens.length - 1" @click="move(index, 1)">
            <VIcon name="chevron-down" :size="18" />
          </VButton>
          <VButton type="icon" variant="ghost" color="red" :title="$t('screens.remove')" :aria-label="$t('screens.remove')" :disabled="screens.length < 2" @click="removeScreen(index)">
            <VIcon name="trash" :size="18" />
          </VButton>
        </div>

        <ScreenPreview :screen="screen" class="mt-3" />

        <div class="mt-3 flex items-center justify-between gap-2 text-xs text-gray-dark-25 dark:text-gray-light-10">
          <span>{{ $t('screens.location') }}: {{ $t(`screens.storage.${screen.location}`) }}</span>
          <span v-if="screen.file" class="truncate font-mono">{{ screen.file }}</span>
        </div>

        <div v-if="screen.type !== 'status'" class="mt-4">
          <div class="flex items-center justify-between gap-2">
            <h4 class="text-sm font-semibold">{{ $t('screens.labels') }} ({{ screen.labels.length }} / {{ maxLabels }})</h4>
            <VButton type="icon" size="xs" variant="outline" color="gray" :title="$t('screens.addLabel')" :aria-label="$t('screens.addLabel')" :disabled="screen.labels.length >= maxLabels" @click="addLabel(screen)">
              <VIcon name="add" :size="14" />
            </VButton>
          </div>

          <p class="mt-1 text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.hint') }}</p>

          <div v-if="!screen.labels.length" class="mt-2 text-xs text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.noLabels') }}</div>

          <div v-for="(label, labelIndex) in screen.labels" :key="labelIndex" class="mt-2 rounded-lg border border-gray-light-10 p-3 dark:border-gray-dark-30">
            <div class="grid grid-cols-3 gap-2 sm:grid-cols-6">
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                X
                <input v-model.number="label.x" type="number" min="0" max="319" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                Y
                <input v-model.number="label.y" type="number" min="0" max="239" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                W
                <input v-model.number="label.w" type="number" min="1" max="320" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                H
                <input v-model.number="label.h" type="number" min="1" max="240" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                {{ $t('screens.slot') }}
                <input v-model.number="label.slot" type="number" min="0" max="7" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                {{ $t('screens.size') }}
                <input v-model.number="label.size" type="number" min="1" max="4" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 text-sm dark:border-gray-dark-30" />
              </label>
            </div>

            <div class="mt-2 grid grid-cols-1 gap-2 sm:grid-cols-[1fr_auto_auto_auto]">
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                {{ $t('screens.text') }}
                <input v-model="label.text" maxlength="63" class="mt-1 w-full rounded-md border border-gray-light-10 bg-transparent px-2 py-1 font-mono text-sm dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                {{ $t('screens.color') }}
                <input v-model="label.color" type="color" class="mt-1 h-9 w-14 cursor-pointer rounded-md border border-gray-light-10 bg-transparent p-1 dark:border-gray-dark-30" />
              </label>
              <label class="text-xs text-gray-dark-25 dark:text-gray-light-10">
                {{ $t('screens.align') }}
                <select v-model="label.align" class="mt-1 h-9 rounded-md border border-gray-light-10 bg-transparent px-2 text-sm dark:border-gray-dark-30">
                  <option v-for="align in alignOptions" :key="align" :value="align">{{ $t(`screens.alignValue.${align}`) }}</option>
                </select>
              </label>
              <VButton type="icon" variant="ghost" color="red" class="mt-4" :title="$t('screens.removeLabel')" :aria-label="$t('screens.removeLabel')" @click="removeLabel(screen, labelIndex)">
                <VIcon name="trash" :size="16" />
              </VButton>
            </div>

            <p v-if="cyrillic(label.text)" class="mt-2 text-xs text-amber-600 dark:text-amber-400">{{ $t('screens.cyrillic') }}</p>
          </div>
        </div>
      </VCard>
    </div>

    <VCard v-else variant="glass" padding="lg">
      <p class="text-sm text-gray-dark-25 dark:text-gray-light-10">{{ $t('screens.empty') }}</p>
    </VCard>

    <ScreenUploadDialog
      v-if="uploadOpen"
      :sd-available="sdAvailable"
      :default-name="`screen-${screens.length + 1}`"
      @close="uploadOpen = false"
      @saved="onSaved"
    />
  </div>
</template>
