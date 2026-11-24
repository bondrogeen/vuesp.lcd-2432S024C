<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';

import { storeToRefs } from 'pinia';
import { useAppStore } from '@/stores/AppStore';

import { getPageTitle } from 'vuesp-components/helpers';

import ServiceGPIO from '@/components/service/ServiceGPIO.vue';
import ServiceScreens from '@/components/service/ServiceScreens.vue';
import ServiceStorage from '@/components/service/ServiceStorage.vue';
import ServiceStorageSD from '@/components/service/ServiceStorageSD.vue';
import ServiceSettings from '@/components/service/ServiceSettings.vue';

const appStore = useAppStore();
const { menu } = storeToRefs(appStore);

const route = useRoute();
const router = useRouter();

const show = ref(false);

const title = computed(() => getPageTitle(menu.value, route.fullPath)?.name);
const page = computed(() => route.params.page);

const isPage = (value: string) => page.value === value;

onMounted(() => {
  if (page.value === 'screens') {
    router.replace('/device/display/');
    return;
  }

  setTimeout(() => {
    show.value = true;
  }, 100);
});
</script>

<template>
  <div>
    <div class="mb-6 flex items-center justify-between">
      <h1>{{ $t(title || '') }}</h1>

      <div id="device-slot"></div>
    </div>

    <template v-if="show">
      <ServiceSettings v-if="isPage('settings')" />

      <div v-if="isPage('storage')" class="grid grid-cols-1 items-start gap-4 xl:grid-cols-2">
        <ServiceStorage />
        <ServiceStorageSD />
      </div>

      <ServiceGPIO v-if="isPage('gpio')" />

      <div v-if="isPage('display')" class="flex flex-col gap-4">
        <ServiceScreens />
      </div>
    </template>
  </div>
</template>
