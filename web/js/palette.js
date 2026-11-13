// Map colours. One palette for light mode and one for dark mode.

const LIGHT = {
  outside: '#cfd3cf',
  land: '#e7e4da',
  ground: '#dcd8cc',
  zone: {
    water: '#9cc6e0', park: '#c4ddb0', residential: '#ece2cf', apartments: '#e7d9c4', commercial: '#efd9cb',
    downtown: '#e2dbe6', industrial: '#dcdcd6', civic: '#dfe7ef',
  },
  water: '#9cc6e0', waterEdge: '#86b4d2',
  roadCasing: '#3f444a',
  road: { 1: '#7a8087', 2: '#6a7078', 3: '#5a6068' },
  bridge: '#c9c2b2',
  marking: 'rgba(250,250,245,0.9)',
  median: '#b9b39f',
  zebra: 'rgba(250,250,245,0.85)',
  building: {
    house: '#d8b48f', apartment: '#c7b39f', shop: '#e2a273', office: '#9bb0c7', warehouse: '#b5b1a7',
    factory: '#a49e93', school: '#e9c35b', hospital: '#f6f6f4', fireStation: '#d9534f', policeStation: '#4f74a8',
    busDepot: '#8f9f5e', mall: '#c497c2', stadium: '#d6cfbf',
  },
  outline: 'rgba(40,30,20,0.28)',
  shadow: 'rgba(40,35,25,0.18)',
  feature: { park: '#bcd9a6', sportsField: '#86bd70', parking: '#c3c1bb', plaza: '#d6cfc0' },
  fieldLine: 'rgba(255,255,255,0.75)',
  tree: '#6fa35a', treeDark: '#5b8c48',
  label: '#20262c', labelHalo: 'rgba(255,255,255,0.88)',
  district: 'rgba(30,36,42,0.30)',
  roadLabel: '#f4f2ea', roadLabelHalo: 'rgba(40,44,50,0.85)',
};

const DARK = {
  outside: '#14171a',
  land: '#1d2126',
  ground: '#262b31',
  zone: {
    water: '#1f3d55', park: '#22382a', residential: '#2c2a26', apartments: '#2d2a29', commercial: '#33292a',
    downtown: '#2a2833', industrial: '#292b2d', civic: '#25303b',
  },
  water: '#1f3d55', waterEdge: '#2b5372',
  roadCasing: '#0f1216',
  road: { 1: '#454b52', 2: '#4c535b', 3: '#555c65' },
  bridge: '#6b6658',
  marking: 'rgba(225,225,215,0.75)',
  median: '#5e5a4e',
  zebra: 'rgba(225,225,215,0.7)',
  building: {
    house: '#8c7056', apartment: '#7f7062', shop: '#94664a', office: '#5b6f86', warehouse: '#6b6862',
    factory: '#5f5a53', school: '#a5893c', hospital: '#c9cdd1', fireStation: '#a23f3c', policeStation: '#3c5a86',
    busDepot: '#5f6b3e', mall: '#7d5f7c', stadium: '#77705f',
  },
  outline: 'rgba(0,0,0,0.35)',
  shadow: 'rgba(0,0,0,0.22)',
  feature: { park: '#2b4630', sportsField: '#356a3a', parking: '#3a3d42', plaza: '#3c3934' },
  fieldLine: 'rgba(230,240,230,0.45)',
  tree: '#3f6e3f', treeDark: '#2f5530',
  label: '#e9ecef', labelHalo: 'rgba(15,18,22,0.85)',
  district: 'rgba(230,235,240,0.22)',
  roadLabel: '#e6e9ec', roadLabelHalo: 'rgba(10,12,15,0.85)',
};

// Signal and junction-control colours are the same in both themes.
export const SIGNAL = { red: '#e5484d', amber: '#f2b632', green: '#35c46a', off: '#2b3036', body: '#15191d' };

// Minimal "white skeleton" look used for the showcase: white roads, faint blocks, no textures.
const G = '#eef0f3';
const MINIMAL = {
  ...LIGHT,
  outside: '#f6f7f9', land: '#f6f7f9', ground: G,
  zone: { water: '#dde9f4', park: '#e4efe2', residential: G, apartments: G, commercial: G, downtown: G, industrial: G, civic: G },
  water: '#dde9f4', waterEdge: '#cfe0ee',
  roadCasing: '#c3c9d2',
  road: { 1: '#ffffff', 2: '#ffffff', 3: '#ffffff' },
  bridge: '#dfe3e8',
  marking: 'rgba(150,158,170,0.7)', median: '#dfe3e8', zebra: 'rgba(170,176,186,0.6)',
  building: Object.fromEntries(Object.keys(LIGHT.building).map(k => [k, '#e1e4e9'])),
  outline: 'rgba(0,0,0,0.05)', shadow: 'rgba(0,0,0,0)',
  feature: { park: '#e4efe2', sportsField: '#e4efe2', parking: '#e8eaee', plaza: '#e8eaee' },
  fieldLine: 'rgba(0,0,0,0)', tree: '#d6e6d3',
  label: '#4a525c', labelHalo: 'rgba(246,247,249,0.95)', district: 'rgba(60,68,80,0.25)',
  roadLabel: '#7a828d', roadLabelHalo: 'rgba(255,255,255,0.95)',
};

export function getPalette() {
  return MINIMAL;
}
export { LIGHT, DARK };
