# Overview

A [VCMI](https://github.com/vcmi/vcmi) AI library which uses pre-trained
models for commanding a hero's army in battle.

During gameplay, MMAI extracts various features from the battlefield,
feeds it to a pre-trained model and executes an action based on the model's
output.

MMAI is also essential during model training, where the collected data is sent
to [`vcmi-gym`](https://github.com/smanolloff/vcmi-gym) (a Reinforcement
Learning environment designed for VCMI).

## Save-game reproducibility

For stochastic models, MMAI stores the complete `std::mt19937` state of the
attacker and defender models in the client's private local-state section of a
save. Loading restores each model independently, so with `temperature > 0` the
same save and the same subsequent human actions produce the same sampled MMAI
choices.

The record is accepted only when its format version, model name, version, path,
and temperature match. Missing records (including old saves), malformed records, or
model mismatches are logged and use the configured initial RNG state. A new
game also resets that state, preventing a process-global model repository from
carrying randomness across games. The standard-library text representation of
`mt19937` is intended for replay with the same build/runtime; portability across
different C++ standard-library implementations is not guaranteed.
