#ifndef IMAGE_HINT_TRANSFORMER_H
#define IMAGE_HINT_TRANSFORMER_H

#include "lms/module.h"
#include "lms/math/polyline.h"
#include "street_environment/road.h"
#include "sensor_utils/car.h"

class TrajectoryLineCreator : public lms::Module {
public:
    bool initialize() override;
    bool deinitialize() override;
    bool cycle() override;
private:
    void simpleTrajectory();
    bool advancedTrajectory();
    const street_environment::EnvironmentObjects *envObstacles;
    const street_environment::RoadLane *road;
    lms::math::polyLine2f *trajectory;
    const lms::ModuleConfig *config;
    float kappa_old;

};

#endif /* IMAGE_HINT_TRANSFORMER_H */
