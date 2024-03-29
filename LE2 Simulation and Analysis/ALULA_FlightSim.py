from rocketpy import Environment, SolidMotor, Rocket, Flight, Function



Env = Environment(
    railLength=5.2,
    latitude=32.9901,
    longitude=-106.9751,
    elevation=1400.556
)



import datetime

tomorrow = datetime.date.today() + datetime.timedelta(days=1)

Env.setDate((tomorrow.year,
             tomorrow.month,
             tomorrow.day,
             12))  # Hour given in UTC time



Env.setAtmosphericModel(type="Forecast", file="GFS")



Env.info()



M1939W = SolidMotor(
    thrustSource="../RocketPy/data/motors/AeroTech_M1939W.eng",
    burnOut=6.2,
    grainNumber=1,
    grainSeparation=1 / 1000,
    grainDensity=1715,
    grainOuterRadius=45 / 1000,
    grainInitialInnerRadius=25 / 1000,
    grainInitialHeight=732 / 1000,
    nozzleRadius=33 / 1000,
    throatRadius=11 / 1000,
    interpolationMethod="linear",
)



M1939W.info()



Calisto = Rocket(
    motor=M1939W,
    radius=0.0785,
    mass=28.103 - 5.719,             # total dry: 30.2
    inertiaI=22.060737,      #
    inertiaZ=0.11227482,     #
    distanceRocketNozzle=1.44969,    #
    distanceRocketPropellant=1.344,  #
    powerOffDrag="../RocketPy/data/calisto/powerOffDragCurveIREC.csv",
    powerOnDrag="../RocketPy/data/calisto/powerOnDragCurveIREC.csv",
)

Calisto.setRailButtons([0.18, -1.4246, 60])

NoseCone = Calisto.addNose(length=0.762, kind="Von Karman", distanceToCM=2.14)

FinSet = Calisto.addTrapezoidalFins(
    n=3,
    rootChord =0.305,
    tipChord=0.102,
    span=0.152,
    distanceToCM=-1.49,
    sweepAngle=33.7
)

# Tail = Calisto.addTail(
#     topRadius=0.157, bottomRadius=0.157, length=0.61, distanceToCM=-1.1081
# )



def drogueTrigger(p, y):
    return True if y[5] < 0 else False

def mainTrigger(p, y):
    return True if y[5] < 0 and y[2] < 305 + 1400.556 else False

Main = Calisto.addParachute('Main',
                            CdS=23.146,
                            trigger=mainTrigger,
                            samplingRate=105,      #
                            lag=1.5,               #
                            noise=(0, 8.3, 0.5))   #

Drogue = Calisto.addParachute('Drogue',
                              CdS=1.44346,
                              trigger=drogueTrigger,
                              samplingRate=105,      #
                              lag=1.5,               #
                              noise=(0, 8.3, 0.5))   #


#                             CdS=15.532,
#                             CdS=2.4926,



Calisto.allInfo()



TestFlight = Flight(rocket=Calisto, environment=Env, inclination=85, heading=0)



TestFlight.allInfo()







