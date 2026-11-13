#include<iostream>
#include<fstream>
#include<string>
#include<cstdlib>
#include<iomanip>
#include<ctime>
using namespace std;

const int ROADS = 4;

// Each road is a row of cells. Cell 0 is at the stop line, the last cell is where vehicles arrive.
const int LANE_LEN = 16;

// After crossing, a vehicle keeps driving for this many ticks. Only used so the viewer can animate it leaving.
const int EXIT_LEN = 6;

// Rush hour brings more vehicles.
const int RUSH_START = 800;
const int RUSH_END = 1000;
const float RUSH_BOOST = 1.5;

string roadName[ROADS] = {"North (School)", "East (Market)", "South (Residential)", "West (Red Zone)"};

// Busy zones like school and red zone matter more to the smart controller.
float roadImportance[ROADS] = {2.0, 1.3, 1.0, 1.5};

// Chance of a new vehicle on each road in one tick.
float spawnRate[ROADS] = {0.10, 0.14, 0.07, 0.08};

// Returns the bigger of two values.
template <class T>
T bigger(T a, T b)
{
    if(a > b)
    {
        return a;
    }
    return b;
}

class Vehicle
{
    protected:
    int id;
    int waitTime;       // ticks spent standing still
    bool overtaking;    // true on the tick an ambulance passes the vehicle in front of it
    static int count;

    public:
    Vehicle()
    {
        count++;
        id = count;
        waitTime = 0;
        overtaking = false;
    }

    // Needed so that "delete" on a Vehicle* also cleans up Car, Bus, etc. properly.
    virtual ~Vehicle()
    {
    }

    virtual float getWeight() = 0;
    virtual char getSymbol() = 0;
    virtual string getName() = 0;

    virtual bool isEmergency()
    {
        return false;
    }

    static void resetCount()
    {
        count = 0;
    }

    void wait()
    {
        waitTime++;
    }

    int getWaitTime()
    {
        return waitTime;
    }

    int getId()
    {
        return id;
    }

    bool isOvertaking()
    {
        return overtaking;
    }

    void setOvertaking(bool value)
    {
        overtaking = value;
    }

    friend ostream& operator<<(ostream &out, Vehicle &v)
    {
        out << v.getName() << " #" << v.id;
        return out;
    }
};

int Vehicle::count = 0;

class Car : public Vehicle
{
    public:
    float getWeight()
    {
        return 1.0;
    }

    char getSymbol()
    {
        return 'c';
    }

    string getName()
    {
        return "Car";
    }
};

class Bike : public Vehicle
{
    public:
    float getWeight()
    {
        return 1.0;
    }

    char getSymbol()
    {
        return 'b';
    }

    string getName()
    {
        return "Bike";
    }
};

// A bus carries many people, so it counts more than a car.
class Bus : public Vehicle
{
    public:
    float getWeight()
    {
        return 3.0;
    }

    char getSymbol()
    {
        return 'B';
    }

    string getName()
    {
        return "Bus";
    }
};

class Ambulance : public Vehicle
{
    public:
    float getWeight()
    {
        return 10.0;
    }

    char getSymbol()
    {
        return 'A';
    }

    string getName()
    {
        return "Ambulance";
    }

    bool isEmergency()
    {
        return true;
    }
};

class Intersection
{
    private:
    Vehicle* lane[ROADS][LANE_LEN];       // NULL means the cell is empty
    Vehicle* exitLane[ROADS][EXIT_LEN];   // vehicles that just crossed, kept only for the animation
    int green;
    int greenTime;
    int nextGreen;
    int switching;

    // Writes one vehicle as [id, road, cell, symbol, overtaking, waitTime] for the viewer.
    void writeVehicle(ostream &out, Vehicle* v, int road, int cell)
    {
        out << "[" << v->getId() << "," << road << "," << cell << ",\"" << v->getSymbol() << "\","
            << (v->isOvertaking() ? 1 : 0) << "," << v->getWaitTime() << "]";
    }

    public:
    Intersection()
    {
        for(int i = 0; i < ROADS; i++)
        {
            for(int j = 0; j < LANE_LEN; j++)
            {
                lane[i][j] = NULL;
            }

            for(int j = 0; j < EXIT_LEN; j++)
            {
                exitLane[i][j] = NULL;
            }
        }

        green = 0;
        greenTime = 0;
        nextGreen = 0;
        switching = 0;
    }

    ~Intersection()
    {
        for(int i = 0; i < ROADS; i++)
        {
            for(int j = 0; j < LANE_LEN; j++)
            {
                delete lane[i][j];
            }

            for(int j = 0; j < EXIT_LEN; j++)
            {
                delete exitLane[i][j];
            }
        }
    }

    int getGreen()
    {
        return green;
    }

    int getGreenTime()
    {
        return greenTime;
    }

    // Number of vehicles on a road.
    int getSize(int road)
    {
        int total = 0;

        for(int i = 0; i < LANE_LEN; i++)
        {
            if(lane[road][i] != NULL)
            {
                total++;
            }
        }

        return total;
    }

    int getTotalSize()
    {
        int total = 0;

        for(int i = 0; i < ROADS; i++)
        {
            total += getSize(i);
        }

        return total;
    }

    // 'G' green, 'Y' yellow, 'R' red
    char getLight(int road)
    {
        if(switching == 0)
        {
            if(road == green)
            {
                return 'G';
            }
            return 'R';
        }

        if(switching == 2 && road == green)
        {
            return 'Y';
        }

        return 'R';
    }

    // Puts a vehicle at the far end of a road. Throws an error if that spot is taken.
    void addVehicle(int road, Vehicle* v)
    {
        if(lane[road][LANE_LEN - 1] != NULL)
        {
            throw string("Road is full");
        }

        lane[road][LANE_LEN - 1] = v;
    }

    // Pressure = weight of every vehicle on the road, which grows as it waits.
    float getPressure(int road)
    {
        float total = 0;

        for(int i = 0; i < LANE_LEN; i++)
        {
            if(lane[road][i] != NULL)
            {
                total += lane[road][i]->getWeight() * (1 + lane[road][i]->getWaitTime() / 30.0);
            }
        }

        return total * roadImportance[road];
    }

    bool hasAmbulance(int road)
    {
        for(int i = 0; i < LANE_LEN; i++)
        {
            if(lane[road][i] != NULL && lane[road][i]->isEmergency())
            {
                return true;
            }
        }

        return false;
    }

    // Gives the road that has an ambulance waiting, or -1 if there is none.
    int ambulanceRoad()
    {
        for(int i = 0; i < ROADS; i++)
        {
            if(hasAmbulance(i))
            {
                return i;
            }
        }

        return -1;
    }

    // Runs one tick. Returns the vehicle that crossed the junction, if any.
    // The intersection still owns that vehicle, so do not delete it.
    Vehicle* update(int wanted)
    {
        bool canGo = false;
        Vehicle* crossed = NULL;

        // Changing the green road takes 2 ticks: yellow, then all red.
        if(switching > 0)
        {
            switching--;

            if(switching == 0)
            {
                green = nextGreen;
                greenTime = 0;
                canGo = true;
            }
        }
        else if(wanted != green)
        {
            switching = 2;
            nextGreen = wanted;
        }
        else
        {
            canGo = true;
        }

        if(canGo)
        {
            greenTime++;
        }

        // The vehicle at the stop line of the green road crosses.
        if(canGo && lane[green][0] != NULL)
        {
            crossed = lane[green][0];
            lane[green][0] = NULL;
        }

        for(int r = 0; r < ROADS; r++)
        {
            // Vehicles that already crossed keep driving out of the junction.
            delete exitLane[r][EXIT_LEN - 1];

            for(int k = EXIT_LEN - 1; k > 0; k--)
            {
                exitLane[r][k] = exitLane[r][k - 1];
            }

            exitLane[r][0] = NULL;

            if(r == green)
            {
                exitLane[r][0] = crossed;
            }

            // Vehicle at the stop line did not cross, so it waits.
            if(lane[r][0] != NULL)
            {
                lane[r][0]->setOvertaking(false);
                lane[r][0]->wait();
            }

            // Everyone else moves one cell forward if the cell ahead is free.
            for(int c = 1; c < LANE_LEN; c++)
            {
                Vehicle* v = lane[r][c];

                if(v == NULL)
                {
                    continue;
                }

                v->setOvertaking(false);

                if(lane[r][c - 1] == NULL)
                {
                    lane[r][c - 1] = v;
                    lane[r][c] = NULL;
                }
                else if(v->isEmergency() && !lane[r][c - 1]->isEmergency())
                {
                    // An ambulance passes the vehicle in front of it.
                    Vehicle* other = lane[r][c - 1];
                    lane[r][c - 1] = v;
                    lane[r][c] = other;
                    v->setOvertaking(true);
                    other->wait();
                }
                else
                {
                    v->wait();
                }
            }
        }

        return crossed;
    }

    // Writes every vehicle on the screen (waiting and leaving) for the viewer.
    void writeVehicles(ostream &out)
    {
        bool first = true;

        for(int r = 0; r < ROADS; r++)
        {
            for(int c = 0; c < LANE_LEN; c++)
            {
                if(lane[r][c] != NULL)
                {
                    if(!first)
                    {
                        out << ",";
                    }
                    first = false;
                    writeVehicle(out, lane[r][c], r, c);
                }
            }

            for(int k = 0; k < EXIT_LEN; k++)
            {
                if(exitLane[r][k] != NULL)
                {
                    if(!first)
                    {
                        out << ",";
                    }
                    first = false;
                    writeVehicle(out, exitLane[r][k], r, -(k + 1));
                }
            }
        }
    }
};

class Controller
{
    public:
    virtual ~Controller()
    {
    }

    virtual int decide(Intersection &inter) = 0;
    virtual string getName() = 0;
};

// Every road gets the same fixed green time.
class FixedController : public Controller
{
    public:
    int decide(Intersection &inter)
    {
        if(inter.getGreenTime() >= 20)
        {
            return (inter.getGreen() + 1) % ROADS;
        }

        return inter.getGreen();
    }

    string getName()
    {
        return "Fixed";
    }
};

// Gives green to the road with the most vehicles.
class BasicController : public Controller
{
    public:
    int decide(Intersection &inter)
    {
        int current = inter.getGreen();
        int best = current;

        if(inter.getGreenTime() < 5)
        {
            return current;
        }

        for(int i = 0; i < ROADS; i++)
        {
            if(inter.getSize(i) > inter.getSize(best) + 1)
            {
                best = i;
            }
        }

        return best;
    }

    string getName()
    {
        return "Basic";
    }
};

// Looks at vehicle weights, waiting time, road importance and ambulances.
class SmartController : public Controller
{
    public:
    int decide(Intersection &inter)
    {
        int current = inter.getGreen();
        int best = current;

        // An ambulance is already on the green road, so keep green until it has crossed.
        if(inter.hasAmbulance(current))
        {
            return current;
        }

        // An ambulance is waiting on another road, so that road gets green.
        int ambulance = inter.ambulanceRoad();

        if(ambulance != -1)
        {
            return ambulance;
        }

        if(inter.getGreenTime() < 5)
        {
            return current;
        }

        for(int i = 0; i < ROADS; i++)
        {
            if(inter.getPressure(i) > inter.getPressure(best))
            {
                best = i;
            }
        }

        // Changing the signal wastes time, so the other road must be much busier.
        if(best != current && inter.getPressure(best) > inter.getPressure(current) * 1.5)
        {
            return best;
        }

        return current;
    }

    string getName()
    {
        return "Smart";
    }
};

class Result
{
    public:
    float avgWait;
    int maxWait;
    int served;
    int blocked;          // vehicles that could not enter because the road was full
    float avgQueue;
    float ambulanceWait;
    int stillWaiting;
};

// Runs the whole simulation for one controller and returns its results.
// If replay is not NULL, every tick is also written to it for the HTML viewer.
Result runSimulation(Controller &controller, int seed, int totalTicks, ostream* replay)
{
    Intersection inter;
    Result r;
    long totalWait = 0;
    long totalQueue = 0;
    long ambulanceTotal = 0;
    int ambulances = 0;

    r.maxWait = 0;
    r.served = 0;
    r.blocked = 0;

    Vehicle::resetCount();

    // Same seed for every controller, so the traffic is exactly the same.
    srand(seed);

    if(replay != NULL)
    {
        *replay << "{name:\"" << controller.getName() << "\",seed:" << seed << ",ticks:" << totalTicks
                << ",laneLen:" << LANE_LEN << ",exitLen:" << EXIT_LEN
                << ",rush:[" << RUSH_START << "," << RUSH_END << "],roads:[";

        for(int i = 0; i < ROADS; i++)
        {
            if(i > 0)
            {
                *replay << ",";
            }
            *replay << "\"" << roadName[i] << "\"";
        }

        *replay << "],frames:[" << endl;
    }

    for(int tick = 0; tick < totalTicks; tick++)
    {
        string events = "";
        float rush = 1.0;

        if(tick >= RUSH_START && tick < RUSH_END)
        {
            rush = RUSH_BOOST;
        }

        // The random numbers are drawn on every tick no matter what happens,
        // so all controllers see the same traffic.
        for(int i = 0; i < ROADS; i++)
        {
            int chance = rand() % 1000;
            int type = rand() % 100;

            if(chance < spawnRate[i] * rush * 1000)
            {
                Vehicle* v;

                if(type < 55)
                {
                    v = new Car();
                }
                else if(type < 85)
                {
                    v = new Bike();
                }
                else
                {
                    v = new Bus();
                }

                try
                {
                    inter.addVehicle(i, v);
                }
                catch(string message)
                {
                    delete v;
                    r.blocked++;
                }
            }
        }

        // Sometimes an ambulance arrives from a random road.
        int ambulanceChance = rand() % 1000;
        int ambulanceRoad = rand() % ROADS;

        if(ambulanceChance < 4)
        {
            Vehicle* a = new Ambulance();

            try
            {
                inter.addVehicle(ambulanceRoad, a);
                events += "\"" + a->getName() + " #" + to_string(a->getId()) + " arrived on " + roadName[ambulanceRoad] + "\",";
            }
            catch(string message)
            {
                delete a;
                r.blocked++;
            }
        }

        Vehicle* crossed = inter.update(controller.decide(inter));

        if(crossed != NULL)
        {
            r.served++;
            totalWait += crossed->getWaitTime();
            r.maxWait = bigger(r.maxWait, crossed->getWaitTime());

            if(crossed->isEmergency())
            {
                ambulances++;
                ambulanceTotal += crossed->getWaitTime();
                events += "\"" + crossed->getName() + " #" + to_string(crossed->getId()) + " crossed after waiting "
                          + to_string(crossed->getWaitTime()) + " ticks\",";
            }
        }

        totalQueue += inter.getTotalSize();

        if(replay != NULL)
        {
            *replay << "{t:" << tick << ",l:\"";

            for(int i = 0; i < ROADS; i++)
            {
                *replay << inter.getLight(i);
            }

            *replay << "\",v:[";
            inter.writeVehicles(*replay);
            *replay << "],s:[" << r.served << "," << totalWait << "," << r.blocked << "," << r.maxWait << ","
                    << inter.getTotalSize() << "],e:[" << events << "]}";

            if(tick < totalTicks - 1)
            {
                *replay << ",";
            }

            *replay << endl;
        }
    }

    if(replay != NULL)
    {
        *replay << "]}";
    }

    r.avgWait = 0;
    r.ambulanceWait = 0;

    if(r.served > 0)
    {
        r.avgWait = (float)totalWait / r.served;
    }

    if(ambulances > 0)
    {
        r.ambulanceWait = (float)ambulanceTotal / ambulances;
    }

    r.avgQueue = (float)totalQueue / totalTicks;
    r.stillWaiting = inter.getTotalSize();

    return r;
}

// Shows the comparison table and also saves it in results.csv.
void showResults(Result r[], string names[], int n)
{
    cout << endl << left << setw(28) << "Controller";

    for(int i = 0; i < n; i++)
    {
        cout << setw(10) << names[i];
    }

    cout << endl << fixed << setprecision(1);

    cout << setw(28) << "Average wait (ticks)";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].avgWait;
    cout << endl << setw(28) << "Maximum wait (ticks)";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].maxWait;
    cout << endl << setw(28) << "Vehicles cleared";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].served;
    cout << endl << setw(28) << "Vehicles turned away";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].blocked;
    cout << endl << setw(28) << "Average vehicles on roads";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].avgQueue;
    cout << endl << setw(28) << "Ambulance wait (ticks)";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].ambulanceWait;
    cout << endl << setw(28) << "Left on roads at the end";
    for(int i = 0; i < n; i++) cout << setw(10) << r[i].stillWaiting;
    cout << endl;

    ofstream file("results.csv");

    if(!file)
    {
        cout << "Could not save results.csv" << endl;
        return;
    }

    file << "Controller,AvgWait,MaxWait,Cleared,TurnedAway,AvgOnRoads,AmbulanceWait,LeftOnRoads" << endl;

    for(int i = 0; i < n; i++)
    {
        file << names[i] << "," << r[i].avgWait << "," << r[i].maxWait << "," << r[i].served << ","
             << r[i].blocked << "," << r[i].avgQueue << "," << r[i].ambulanceWait << "," << r[i].stillWaiting << endl;
    }

    file.close();
    cout << endl << "Results saved in results.csv" << endl;
}

// Opens viewer.html in the default browser (Windows only).
void openViewer()
{
    cout << endl << "Replay saved in replay.js. Open viewer.html in your browser to watch it." << endl;

#if defined(_WIN32) && !defined(NO_OPEN)
    system("start \"\" viewer.html");
#endif
}

// Reads a whole number. If the user types something else it asks again,
// and if the input ends it quits (otherwise bad input would loop forever).
int readNumber()
{
    int value;

    while(!(cin >> value))
    {
        if(cin.eof())
        {
            exit(0);
        }

        cin.clear();
        cin.ignore(1000, '\n');
        cout << "Please enter a number: ";
    }

    return value;
}

int main()
{
    int choice, seed, totalTicks;

    cout << "Adaptive Traffic Control Simulator" << endl;
    cout << "Enter seed number (0 = random): ";
    seed = readNumber();

    if(seed == 0)
    {
        seed = (int)time(NULL);
        cout << "Using seed " << seed << " (enter it next time to get the same traffic)" << endl;
    }

    cout << "Enter number of ticks to simulate (for example 2000): ";
    totalTicks = readNumber();

    if(totalTicks <= 0)
    {
        totalTicks = 2000;
        cout << "Using 2000 ticks" << endl;
    }

    do
    {
        cout << endl << "Enter 1 to run one controller and watch it in the browser" << endl
             << "Enter 2 to compare all controllers (table, results.csv and browser replay)" << endl
             << "Enter 0 to exit" << endl;
        choice = readNumber();

        switch(choice)
        {
            case 1:
            {
                int type;
                Controller* controller;

                cout << "Enter 1 for Fixed, 2 for Basic, 3 for Smart: ";
                type = readNumber();

                if(type == 1)
                {
                    controller = new FixedController();
                }
                else if(type == 2)
                {
                    controller = new BasicController();
                }
                else if(type == 3)
                {
                    controller = new SmartController();
                }
                else
                {
                    cout << "Enter valid choice number" << endl;
                    break;
                }

                ofstream replay("replay.js");

                if(!replay)
                {
                    cout << "Could not create replay.js" << endl;
                    delete controller;
                    break;
                }

                replay << "const REPLAYS = [" << endl;
                runSimulation(*controller, seed, totalTicks, &replay);
                replay << endl << "];" << endl;
                replay.close();

                delete controller;
                openViewer();
                break;
            }

            case 2:
            {
                Controller* controllers[3];
                string names[3] = {"Fixed", "Basic", "Smart"};
                Result results[3];

                controllers[0] = new FixedController();
                controllers[1] = new BasicController();
                controllers[2] = new SmartController();

                ofstream replay("replay.js");
                bool saved = (bool)replay;

                if(saved)
                {
                    replay << "const REPLAYS = [" << endl;
                }
                else
                {
                    cout << "Could not create replay.js" << endl;
                }

                for(int i = 0; i < 3; i++)
                {
                    if(saved)
                    {
                        if(i > 0)
                        {
                            replay << "," << endl;
                        }
                        results[i] = runSimulation(*controllers[i], seed, totalTicks, &replay);
                    }
                    else
                    {
                        results[i] = runSimulation(*controllers[i], seed, totalTicks, NULL);
                    }

                    delete controllers[i];
                }

                showResults(results, names, 3);

                if(saved)
                {
                    replay << endl << "];" << endl;
                    replay.close();
                    openViewer();
                }

                break;
            }

            case 0:
                break;

            default:
                cout << "Enter valid choice number" << endl;
        }
    } while(choice != 0);

    return 0;
}
