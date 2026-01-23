
# Tactical Crowd AI Toolkit Documentation


## Overview
---
The Tactical Crowd AI Toolkit enables the creation of large-scale AI systems that make informed movement and navigation decisions using **influence maps**.
Designed for performance, it scales to hundreds -- if not thousands -- of simultaneous units, making it ideal for crowd-level decision making where traditional query-based approaches struggle.


## Features
---
- Includes an **Influence Volume** and **Influence Source Component** to create influence maps with just a few clicks.
- Computes influence maps every tick, dynamically switching between **GPU and CPU** based on detected bottlenecks.
- Supports **composite operations** on base influence maps, including addition, subtraction, multiplication, normalization, etc.
- Provides intuitive **Blueprints** and **Behavior Tree Services** for querying influence maps asynchronously.


## Usage
---
### Basic Usage
1. In **Project Settings**, find **Plugins - TCAT Global Settings**. Add an influence **tag** (e.g. 'Enemy' or 'Citizen')
2. Add an **Influence Volume** into the level. Under **TCAT**, add an element to the **Base Layer Configs** array and assign it the tag from step 1.
3. Add an **Influnece Component** to the source actor. Under **TCAT**, add an element to the **Influence Layer Map** array and assign it the desired tag. Here, you can configure influence radius, strength, and other properties. 
4. Use the influence map for decision-making:
	1. For **Blueprints**, refer to the *Blueprints* section below.
	2. For **Behavior Trees**, refer to the *Behavior Trees* section below.

### Blueprints
These queries are done asynchronously by default. These nodes leverage an optimized batch processing system, ensuring high performance even when handling hundreds of simultaneous queries.
#### Nodes
- **Search Highest Value**
	- Searches for the highest influence value within a specified radius.
- **Search Highest Values (Multi)**
	- Finds the top *N* highest influence values within a specified radius.
- **Search Lowest Value**
	- Searches for the lowest influence value within a specified radius.
- **Search Lowest Values (Multi)**
	- Finds the top *N* lowest influence values within a specified radius.
- **Search Condition**
	- Returns any location within the radius that meets a specific condition. (e.g. "Find any spot with > 50 influence" or "Are there any health items nearby?").
- **Get Value At Component**
	- Retrieves the influence value at the component's current location.
- **Get Influence Gradient**
	- Calculates the influence gradient (slope) at the component's location. Useful for determining the direction of increasing or decreasing influence (e.g. "Which way is safer?").
- **Search Highest In Condition**
	- Searches for the highest influence value, while only considering areas that meet a specific condition. (e.g. "Find the safest point (Highest Safety) where the fire damage is zero (Condition < 0)").
- **Search Highest Values In Condition (Multi)**
	- Searches for top *N* highest influence values, while only considering areas that meet a specific condition.
- **Search Lowest In Condition**
	- Searches for the lowest influence value, while only considering areas that meet a specific condition. (e.g. "Find the point with least enemy presence (Lowest Threat) that is also within attack range (Condition < Range)").
- **Search Lowest Values In Condition (Multi)**
	- Searches for top *N* lowest influence values, while only considering areas that meet a specific condition.

### Behavior Trees
This plugin provides both a **Behavior Tree Service** and a **Behavior Tree Task** that interface with the same asynchronous influence query system. Both nodes expose identical **Query Modes** and **filter options**, allowing influence-based decisions to be evaluated continuously (Service) or executed as a one-off action (Task).
#### Provides:
- **TCAT Async Query Service**
- **TCAT Async Query Task**
#### Query Modes:
- **Find Highest Value**
	- Searches for the location with the highest influence value within the specified query radius.
	- Optional conditional filtering can be enabled to ignore values that do not meet a comparison threshold (e.g., “only consider values greater than 0.5”).
- **Find Lowest Value**
	- Searches for the location with the lowest influence value within the specified query radius.
	- Supports the same conditional filtering as **Find Highest Value**, enabling queries such as "find the lowest threat above zero."
- **Check Condition**
	- Evaluates whether any location within the query radius satisfies a specified condition using a comparison operator and threshold value (e.g., “is there any position with influence > 50?”).
- **Get Value at Position**
	- Samples the influence map at the query position and returns the resulting value. Useful for evaluating the AI’s current location or a known target position.
- **Get Influence Gradient**
	- Computes the local influence gradient at the query position, indicating the direction of increasing or decreasing influence.
	- Commonly used for steering behaviors such as moving toward safety or away from danger.

### Environment Query System (EQS) Integration
This plugin integrates with Unreal Engine’s **Environment Query System (EQS)** by providing a custom **Grid Generator** and **Influence Test** that operate directly on TCAT influence maps. This allows EQS to sample candidate locations from TCAT’s spatial grid and score them using influence values, combining the flexibility of EQS with the performance and scalability of influence-map-based decision making.
#### Provides:
- **EnvQueryGenerator_TCATGrid**
	- Generates a 2D grid of query points around a context location, constrained to a TCAT Influence Volume.
- **EnvQueryTest_TCATInfluence**
	- Scores EQS items based on influence values stored in a TCAT Influence Volume.


## Technical Details
---
#### File Structure
```
TCAT/
├── Shaders/
│   ├── TCAT_CompositeMap.usf
│   ├── TCAT_FindMinMax.usf
│   ├── TCAT_InfluenceCommon.ush
│   └── TCAT_UpdateInfluence.usf
│
├── Source/
│   └── TCAT/
│       ├── Public/
│       │   ├── Core/
│       │   │   ├── TCATMathLibrary.h
│       │   │   ├── TCATSettings.h
│       │   │   └── TCATSubsystem.h
│       │   ├── Query/
│       │   │   ├── BT/
│       │   │   │   ├── BTService_TCATAsyncQuery.h
│       │   │   │   └── BTTask_TCATAsyncQuery.h
│       │   │   ├── EQS/
│       │   │   │   ├── EnvQueryGenerator_TCATGrid.h
│       │   │   │   └── EnvQueryTest_TCATInfluence.h
│       │   │   ├── TCATAsyncMultiSearchAction.h
│       │   │   ├── TCATAsyncQueryAction.h
│       │   │   ├── TCATQueryProcessor.h
│       │   │   └── TCATQueryTypes.h
│       │   ├── Scene/
│       │   │   ├── TCATInfluenceComponent.h
│       │   │   ├── TCATInfluenceVolume.h
│       │   │   └── TCATHeightMapModule.h
│       │   ├── Simulation/
│       │   │   ├── TCATAsyncResourceRingBuffer.h
│       │   │   ├── TCATCompositeLogic.h
│       │   │   ├── TCATGridResource.h
│       │   │   └── TCATInfluenceDispatcher.h
│       │   └── TCAT.h
│       │
│       ├── Private/
│       │   ├── ...
│
├── Resources/
│   └── Icon128.png
│   └── InfluenceComponentIcon.png
│
└── README.md
```
#### Dependencies
- No external dependencies required.


## Installation Requirements
---
- Unreal Engine 5.6 or later
- Windows operating system


## Support
---
Contact: over2ktech@gmail.com


## License
---
This plugin is provided under the MIT License. See the LICENSE file for details.