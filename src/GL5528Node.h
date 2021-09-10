//
// Created by muttley on 22/08/21.
//

#pragma once

#include <AdcNode.hpp>

// Extend due the convert ADC analog read to Lux
class GL5528Node : public AdcNode {
public:
    using AdcNode::AdcNode;

    void setup() override {

        advertise(getId())
                .setDatatype("float")
                .setFormat("0:1.00")
                .setUnit(cUnitLux);
    };

    float readMeasurement() override {

        float Vr2 = AdcNode::readMeasurement();

        // Two voltage divider:
        // * 1st with Rp(photoresistor) and R2
        //   Vout = Vr2 = Vin * R2/(Rp+R2) => Rp = (3.3*R2/Vout) - R2 = (3.3*460/Vout) - 460
        // * 2nd in Nodemcu ADC input: range [0,3.3]v, instead of bare esp8266 [0,1]v
        //   Vout = 3.3*Vr2 (scale range: 3.3v instead of 1v)
        // Rp = (3.3*460/3.3*Vr2) - 460 = 460/Vr2 - 460
        float Rp = (460 / Vr2) - 460;

        // Resistance (Ohm) => Luminance (Lux)
        // see the GL5528 datasheet (fig.2)
        // we have a log-log scale (https://en.wikipedia.org/wiki/Log%E2%80%93log_plot)
        // y = 100 - x (y = mx + b) on a linear scale ...should be y = 100 - 0.98x
        // log(lux) = log(100) - log(Rp) => lux = 100/Rp (Rp in KOhm)
        float lux = 100 / (Rp / 1000);

        //Homie.getLogger() << "Luminance - KOhm: " << kOhm << " lux: " << lux << endl;

        return lux;
    }

};
