#include "trajectory_line_creator.h"
#include "lms/datamanager.h"
#include "lms/math/math.h"
#include "street_environment/obstacle.h"
#include "street_environment/crossing.h"
bool TrajectoryLineCreator::initialize() {
    envObstacles = readChannel<street_environment::EnvironmentObjects>("ENVIRONMENT_OBSTACLE");
    road = readChannel<street_environment::RoadLane>("ROAD");
    trajectory = writeChannel<street_environment::Trajectory>("LINE");
    car = readChannel<sensor_utils::Car>("CAR");

    generator = new TrajectoryGenerator(logger);

    return true;
}

bool TrajectoryLineCreator::deinitialize() {
    delete generator;
    return true;
}
bool TrajectoryLineCreator::cycle() {
    //clear old trajectory
    trajectory->points().clear();
    //calculate data for creating the trajectory
    float trajectoryMaxLength = config().get<float>("trajectoryMaxLength",2);
    int obstacleTrustThreshold = config().get<int>("obstacleTrustThreshold",10);
    //float endX;
    //float endY;
    //TODO
    float endVx = 0;
    float endVy = 0;
    //TODO not smart
    lms::math::polyLine2f traj;
    if(config().get<bool>("simpleTraj",true)){
        traj= simpleTrajectory(trajectoryMaxLength,endVx,endVy,obstacleTrustThreshold);
    }else{
        if(!advancedTrajectory(traj)){
            traj = simpleTrajectory(trajectoryMaxLength,endVx,endVy,obstacleTrustThreshold);
        }
    }
    trajectory->points().clear();
    for(lms::math::vertex2f &v:traj.points()){
        trajectory->points().push_back(v);
    }
    return true;
}

bool TrajectoryLineCreator::advancedTrajectory(lms::math::polyLine2f &trajectory){
    if(road->polarDarstellung.size() < 8){
        return false;
    }
    //TODO Blinker setzen

    //INPUT
    //hindernisse: vector Abstand-Straße,geschwindigkeit-Straße(absolut), -1 (links) +1 (rechts) (alle hindernisse hintereinander)
    //eigenes auto, vx,vy, dw -winkelgeschwindigkeit dw (zunächst mal 0)
    //vector mit x-koordinaten
    //vector mit y-koordinaten

    int nSamplesTraj = config().get<int>("nSamplesTraj",50);
    double v1 = 2;//endgeschwindigkeit
    double d1 = -0.2;//TODO berechnenAbweichung, die man am Ende haben will
    //aktuelle daten der fahrspur und des autos relativ dazu
    RoadData dataRoad;
    dataRoad.ax0 = 0; //beschl. am anfang

    dataRoad.kappa = generator->circleCurvature(road->points()[1],road->points()[3],road->points()[5]);//->polarDarstellung[4]+road->polarDarstellung[7]+road->polarDarstellung[9];//TODO
    //dataRoad.kappa /= 3.0;
    logger.info("kappa")<<dataRoad.kappa;
    dataRoad.phi = road->polarDarstellung[1];
    float velocity = 0.001;//Sollte nicht 0 sein, wegen smoothem start
    if(car->velocity() != 0)
        velocity = car->velocity();
    dataRoad.vx0 = velocity;
    dataRoad.w = 0; //aktuelle winkelgeschwindigkeit
    dataRoad.y0 = road->polarDarstellung[0]; //TODO
    std::vector<ObstacleData> dataObstacle;
    CoeffCtot tot;

    tot.kj = config().get<double>("kj",1.0);
    tot.kT = config().get<double>("kT",1.0);
    tot.ks = config().get<double>("ks",1.0);
    tot.kd = config().get<double>("kd",1.0);

    double tMin = 0.1;//Minimal benötigte Zeit
    double tMax = 2; //Maximal benötigte Zeit

    double safetyS = config().get<double>("safetyS",0.1); //Sicherheitsabstand tangential zur Straße
    double safetyD = config().get<double>("safetyD",0.1); //Sicherheitsabstand orthogonal zur Straße

    Trajectory result;
    if(generator->createTrajectorySample(result,v1,d1,safetyS,safetyD,tMin,tMax,nSamplesTraj,dataRoad,dataObstacle,tot)){
        //convert data
        //TODO anzahl der punkte
        points2d<20> points = result.sampleXY<20>();
        for(int i = 0; i < 20; i++){
            trajectory.points().push_back(lms::math::vertex2f(points.x(i),points.y(i)));
        }
        return true;
    }else{
        return false;
    }
    return true;
}

street_environment::Trajectory TrajectoryLineCreator::simpleTrajectory(float trajectoryMaxLength,float &endVx,float &endVy,const int obstacleTrustThreshold){
    //TODO use trajectoryMaxLength
    //TODO Blinker setzen

    street_environment::Trajectory tempTrajectory;
    // translate the middle lane to the right with a quarter of the street width
    const float translation = config().get<float>("street.width", 0.8)/4.0f;
    //TODO das sollte von der aktuellen geschwindigkeit abhängen!
    float distanceObstacleBeforeChangeLine = 0.4;

    using lms::math::vertex2f;
    if(road->points().size() == 0){
        logger.error("cycle") << "no valid environment given";
        return tempTrajectory;
    }

    const street_environment::RoadLane &middle = *road;
    float currentTrajectoryLength = 0;
    bool lastWasLeft = false;
    for(size_t i = 1; i < middle.points().size(); i++) {
        vertex2f p1 = middle.points()[i - 1];
        vertex2f p2 = middle.points()[i];
        if(p1 == p2)
            continue;

        vertex2f along = p2 - p1;
        //check if the trajectory is long enough
        //TODO, get endpoint
        currentTrajectoryLength +=along.length();
        if(currentTrajectoryLength > trajectoryMaxLength){
            break;
        }
        vertex2f mid((p1.x + p2.x) / 2., (p1.y + p2.y) / 2.);
        vertex2f normAlong = along / along.length();
        vertex2f orthogonal(normAlong.y, -normAlong.x);

        bool left = false;
        //man geht davon aus, dass die Abstand, in dem man ausweicht deutlich größer ist als das hinderniss lang!
        float obstacleLength = 0.3;
        //check all obstacles
        //TODO not smart at all, won't work in all cases
        for(const std::shared_ptr<street_environment::EnvironmentObject> obj : envObstacles->objects){
            if(obj->getType() == street_environment::Obstacle::TYPE){
                const street_environment::Obstacle &obst = obj->getAsReference<const street_environment::Obstacle>();
                //check if the obstacle is trusted
                if(obst.trust() < obstacleTrustThreshold){
                    continue;
                }
                float x = obst.position().x;
                float y= obst.position().y;
                if(x < 0){
                    x += obstacleLength;
                }
                if(pow(x*x+y*y,0.5)-mid.length() < distanceObstacleBeforeChangeLine ){
                    left = true;
                    break;
                }
            }else if(obj->getType() == street_environment::Crossing::TYPE){
                const street_environment::Crossing &crossing = obj->getAsReference<const street_environment::Crossing>();
                //check if the Crossing is close enough
                //TODO
                if(crossing.getStreetDistanceTangential() < trajectoryMaxLength){
                    endVx = 0;
                    endVy = 0;
                }
                //add endPoint
            }else{
                //I don't care about astartLine/whatever
                continue;
            }
        }

        if(left){
            orthogonal *= -1;
        }
        //set line indicator
        if(i != 1){
            if(lastWasLeft != left){
                tempTrajectory.addChange(tempTrajectory.points().size(),left);
            }
        }
        lastWasLeft=left;

        orthogonal = orthogonal * translation;
        vertex2f result = mid + orthogonal;
        tempTrajectory.points().push_back(result);
    }
    //remove invalid points
    tempTrajectory.reduce([](const lms::math::vertex2f& p1){
        return p1.x < 0;
    });
    return tempTrajectory;

}

