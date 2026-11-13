// Side panels: stats, legend and the hover tooltip.

import { BUILDING_LABELS, BUILDING_TYPES, CONTROL_LABELS } from './city.js';
import { SIGNAL } from './palette.js';

const $ = id => document.getElementById(id);
const fmt = n => Math.round(n).toLocaleString('en-IN');
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

export function renderStats(s) {
  const items = [
    [s.areaKm2.toFixed(1) + ' km²', 'Area'],
    [s.roadKm.toFixed(1) + ' km', 'Roads'],
    [fmt(s.signals), 'Traffic signals'],
    [fmt(s.allWayStops), 'All-way stops'],
    [fmt(s.population), 'Residents'],
    [fmt(s.jobs), 'Jobs'],
    [fmt(s.buildings), 'Buildings'],
    [fmt(s.bridges), 'Bridges'],
    [`${s.schools} · ${fmt(s.schoolSeats)} seats`, 'Schools', true],
    [`${s.hospitals} · ${fmt(s.hospitalBeds)} beds`, 'Hospitals', true],
    [`${s.fireStations} fire · ${s.policeStations} police · ${s.busDepots} depot`, 'Services', true],
  ];
  $('stats').innerHTML = items
    .map(([v, k, wide]) => `<div class="stat${wide ? ' wide' : ''}"><b>${esc(v)}</b><span>${esc(k)}</span></div>`)
    .join('');
}

export function renderLegend(P) {
  const sw = (color, label, wide) => `<div class="${wide ? 'wide' : ''}"><i style="background:${color}"></i>${esc(label)}</div>`;
  const rows = [
    sw(P.road[3], 'Arterial (2+2 lanes)', true),
    sw(P.road[2], 'Collector'),
    sw(P.road[1], 'Local street'),
    sw(P.zone.residential, 'Residential'),
    sw(P.zone.apartments, 'Apartments'),
    sw(P.zone.commercial, 'Commercial'),
    sw(P.zone.downtown, 'Downtown'),
    sw(P.zone.industrial, 'Industrial'),
    sw(P.feature.park, 'Park'),
    sw(P.building.house, 'House'),
    sw(P.building.office, 'Office'),
    sw(P.building.shop, 'Shop'),
    sw(P.building.warehouse, 'Warehouse'),
    sw('#ffb300', 'School bus'),
    sw('#ff2d2d', 'Ambulance'),
    sw(SIGNAL.green, 'Signal junction'),
    sw('#d93636', 'All-way stop'),
  ];
  $('legend').innerHTML = rows.join('');
}

export function tooltipHtml(hit, city) {
  if (hit.kind === 'building') {
    const b = hit.building;
    const label = BUILDING_LABELS[BUILDING_TYPES.indexOf(b.type)];
    const lines = [`<b>${esc(b.name ?? label)}</b>`];
    if (b.name) lines.push(`<span class="k">${esc(label)}</span>`);
    lines.push(`<span class="k">Floors</span> ${b.floors}`);
    if (b.residents) lines.push(`<span class="k">Residents</span> ${fmt(b.residents)}`);
    if (b.jobs) lines.push(`<span class="k">Jobs</span> ${fmt(b.jobs)}`);
    if (b.capacity) {
      const what = { school: 'Seats', hospital: 'Beds', busDepot: 'Buses', stadium: 'Seats', fireStation: 'Engines', policeStation: 'Patrol cars' }[b.type] ?? 'Capacity';
      lines.push(`<span class="k">${what}</span> ${fmt(b.capacity)}`);
    }
    if (b.district >= 0) lines.push(`<span class="k">Area</span> ${esc(city.districts[b.district].name)}`);
    if (b.accessEdge >= 0) lines.push(`<span class="k">Entrance on</span> ${esc(city.edges[b.accessEdge].name)}`);
    return lines.join('<br>');
  }
  if (hit.kind === 'junction') {
    const n = hit.node;
    const names = [...new Set(n.edges.map(id => city.edges[id].name))];
    return [`<b>${esc(names.join(' × '))}</b>`, `<span class="k">Control</span> ${esc(CONTROL_LABELS[n.control])}`,
      `<span class="k">Approaches</span> ${n.edges.length}`].join('<br>');
  }
  const e = hit.edge;
  const cls = { 1: 'Local street', 2: 'Collector road', 3: 'Arterial road' }[e.cls];
  const lanes = e.lanes * 2 + ' lanes' + (e.median > 0 ? ', divided' : '');
  return [`<b>${esc(e.name)}</b>`, `<span class="k">${cls}</span> ${lanes}`,
    `<span class="k">Speed limit</span> ${Math.round(e.speed * 3.6)} km/h`,
    `<span class="k">Length</span> ${Math.round(e.length)} m${e.bridge ? ' · bridge' : ''}`].join('<br>');
}

const minutes = m => m < 1 ? Math.round(m * 60) + ' s' : m.toFixed(1) + ' min';

export function renderLiveStats(s) {
  const items = [
    [fmt(s.vehicles), 'On the road'],
    [fmt(s.arrived), 'Trips completed'],
    [minutes(s.avgTripMin), 'Average trip'],
    [minutes(s.avgDelayMin), 'Average delay'],
    [s.avgSpeedKmh.toFixed(1) + ' km/h', 'Average speed'],
    [Math.round(s.stoppedShare * 100) + '%', 'Standing still'],
  ];
  items.push([s.ambulanceTrips ? minutes(s.ambulanceAvgMin) : '–', `Ambulance trip (${s.ambulanceTrips ?? 0})`]);
  items.push([s.schoolBusTrips ? minutes(s.schoolBusAvgMin) : '–', `School bus trip (${s.schoolBusTrips ?? 0})`]);
  if (s.pending > 0) items.push([fmt(s.pending), 'Waiting to pull out']);
  if (s.removedStuck > 0) items.push([fmt(s.removedStuck), 'Cleared from gridlock']);
  $('liveStats').innerHTML = items.map(([v, k]) => `<div class="stat"><b>${esc(v)}</b><span>${esc(k)}</span></div>`).join('');

  const w = s.waitingAt ?? {};
  const rows = [['At signals', w.signal], ['At give-way', w.giveWay], ['At stop signs', w.stopSign], ['In queues', (w.bend ?? 0) + (w.insideJunction ?? 0)]];
  const max = Math.max(1, ...rows.map(r => r[1] ?? 0));
  $('waiting').innerHTML = '<div style="margin-bottom:4px">Where vehicles are waiting</div>' + rows.map(([label, n]) =>
    `<div class="bar"><span>${label}</span><div class="track"><div class="fill" style="width:${((n ?? 0) / max) * 100}%"></div></div><span class="n">${fmt(n ?? 0)}</span></div>`).join('');
  $('controllerName').textContent = (s.controller ?? 'Fixed-time') + ' signals';
}

export function renderTracked(t) {
  const card = $('trackedCard');
  if (!t) { card.hidden = true; return; }
  card.hidden = false;
  const elapsed = t.elapsedS, delay = Math.max(0, elapsed - t.freeFlowS * t.progress);
  $('tracked').innerHTML = [
    `<b>${esc(t.kind)} #${t.id}</b> · ${t.speedKmh.toFixed(0)} km/h`,
    `<span class="k">From</span> ${esc(t.from)}`,
    `<span class="k">To</span> ${esc(t.to)}`,
    `<span class="k">Driving</span> ${minutes(elapsed / 60)} · <span class="k">waited</span> ${minutes(t.waitS / 60)} · ${t.stops} stops`,
    `<span class="k">Delay so far</span> about ${minutes(delay / 60)}`,
    `<div class="prog"><i style="width:${Math.round(t.progress * 100)}%"></i></div>`,
  ].join('<br>');
}

export function showTip(html, x, y) {
  const tip = $('tip');
  tip.innerHTML = html;
  tip.hidden = false;
  const w = tip.offsetWidth, h = tip.offsetHeight;
  tip.style.left = Math.min(innerWidth - w - 8, x + 14) + 'px';
  tip.style.top = Math.max(8, Math.min(innerHeight - h - 8, y + 14)) + 'px';
}

export function hideTip() { $('tip').hidden = true; }
