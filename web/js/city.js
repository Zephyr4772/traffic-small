// Turns the compact JSON from the C++ engine (see engine/src/world/CityJson.cpp) into objects.
// Enum numbers must match engine/src/world/City.h.

export const RoadClass = { Local: 1, Collector: 2, Arterial: 3 };
export const Control = { None: 0, Priority: 1, AllWayStop: 2, Signal: 3 };
export const ZONES = ['water', 'park', 'residential', 'apartments', 'commercial', 'downtown', 'industrial', 'civic'];
export const BUILDING_TYPES = ['house', 'apartment', 'shop', 'office', 'warehouse', 'factory',
  'school', 'hospital', 'fireStation', 'policeStation', 'busDepot', 'mall', 'stadium'];
export const BUILDING_LABELS = ['House', 'Apartment block', 'Shop', 'Office', 'Warehouse', 'Factory',
  'School', 'Hospital', 'Fire station', 'Police station', 'Bus depot', 'Mall', 'Stadium'];
export const FEATURES = ['park', 'sportsField', 'parking', 'plaza', 'trees'];
export const CONTROL_LABELS = ['No control', 'Priority (minor road gives way)', 'All-way stop', 'Traffic signal'];

function points(arr, start) {
  const pts = [];
  for (let i = start; i + 1 < arr.length; i += 2) pts.push({ x: arr[i], y: arr[i + 1] });
  return pts;
}

export function decodeCity(raw) {
  const nodes = raw.nodes.map((n, id) => ({ id, x: n[0], y: n[1], control: n[2], stop: n[3], edges: [] }));

  const edges = raw.edges.map((e, id) => {
    const edge = {
      id, a: e[0], b: e[1], cls: e[2], lanes: e[3], laneWidth: e[4], median: e[5],
      bridge: e[6] === 1, name: raw.roadNames[e[7]], speed: e[8],
    };
    edge.halfWidth = edge.lanes * edge.laneWidth + edge.median / 2;
    const A = nodes[edge.a], B = nodes[edge.b];
    edge.length = Math.hypot(B.x - A.x, B.y - A.y);
    A.edges.push(id);
    B.edges.push(id);
    return edge;
  });

  const blocks = raw.blocks.map((b, id) => ({ id, zone: ZONES[b[0]], district: b[1], poly: points(b, 2) }));

  const buildings = raw.buildings.map((b, id) => ({
    id, type: BUILDING_TYPES[b[0]], typeIndex: b[0], floors: b[1], residents: b[2], jobs: b[3], capacity: b[4],
    district: b[5], name: b[6] >= 0 ? raw.names[b[6]] : null,
    accessEdge: b[7], accessT: b[8], accessSide: b[9], poly: points(b, 10),
  }));
  for (const b of buildings) {
    let cx = 0, cy = 0;
    for (const p of b.poly) { cx += p.x; cy += p.y; }
    b.cx = cx / b.poly.length;
    b.cy = cy / b.poly.length;
  }

  const features = raw.features.map(f => ({ type: FEATURES[f[0]], over: f[1] === 1, poly: points(f, 2) }));
  const districts = raw.districts.map((d, id) => ({ id, name: d[0], x: d[1], y: d[2] }));
  const river = { width: raw.river.width, path: points(raw.river.path, 0) };

  return {
    config: raw.config, width: raw.width, height: raw.height,
    nodes, edges, blocks, buildings, features, districts, river, stats: raw.stats,
  };
}
