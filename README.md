# Smart Traffic City

A city-scale traffic simulator with a priority-aware, network-aware traffic signal controller.
The simulation engine is C++17; the city is drawn top-down in the browser.

## Run it

```
build.bat      # first time, or after changing C++ code
run.bat        # starts the engine and opens http://localhost:8080
```

Needs a C++17 compiler with CMake and Ninja. WinLibs GCC (`winget install BrechtSanders.WinLibs.POSIX.UCRT`) ships all three.

Other options:

```
build\traffic.exe --port 9000      use another port
build\traffic.exe --no-open        don't open the browser
build\traffic.exe --selftest       generate 75 cities and check they are valid
build\traffic.exe --simtest 1 4000 simulate 1 hour at 4000 trips/hour without a browser
build\traffic.exe --dump city.json write one city to a file
```

## How the traffic works (milestone 2)

- **Network** (`engine/src/sim/Network.*`): each road becomes one-way links with lanes (lane 0 = kerb lane,
  the left lane in India). Junctions get curved connectors for every allowed turn; connectors that
  cross or merge are marked as conflicting.
- **Driving** (`Simulation.*`): vehicles follow the Intelligent Driver Model. The front vehicle of
  each lane must "claim" its connector before entering a junction; a claim is refused while a
  conflicting movement is in use, on red, when a give-way rule applies, or when the exit road has no
  room (no blocking the box). Vehicles change lanes to be in the right lane for their next turn.
- **Junction rules**: signals (fixed-time, each approach in turn), all-way stops (first come, first
  served) and give-way junctions (minor road yields to major; gap acceptance for turns across traffic).
- **Routing** (`Router.*`): A* over links, counting junction delays and turn costs.
- **Live stream** (`SimulationHost.*`, `net/WebSocket.*`): the simulation runs on its own thread and
  streams binary frames (~15/s) to the browser, which interpolates vehicle positions smoothly.

## Layout

```
engine/src/core     random numbers, geometry, JSON writer
engine/src/world    city model and procedural city generator
engine/src/net      small HTTP server (no dependencies)
engine/src/main.cpp entry point, API routes, self-test
web/                browser front end (canvas renderer, panels)
legacy/             the original single-file prototype
```

## Roadmap

| # | Milestone | Status |
|---|---|---|
| 1 | Procedural city: road hierarchy, river and bridges, districts, zoning, landmarks, buildings; top-down renderer | done |
| 2 | Vehicles with realistic car-following (IDM) on A* routes, fixed-time signals, live stream to the browser | done |
| 3 | Demand model: population, daily schedules, day clock; buses, school buses, ambulances with missions | next |
| 4 | Real intersections: phases, sensors, actuated and max-pressure control, spillback protection | |
| 5 | Priority engine: emergency preemption, time/location/vehicle rules, starvation guard, "why green?" view | |
| 6 | Network layer: district coordinator, green waves, quick-clear, perimeter gating | |
| 7 | Incidents, weather, events, metrics dashboard, batch comparisons of controllers | |

Traffic drives on the left (India). Scale is chosen before generating (Small / Medium / Large / Custom);
traffic volume will be adjustable live once vehicles exist.
