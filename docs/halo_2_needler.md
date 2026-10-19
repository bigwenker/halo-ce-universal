# Fork Needler rebalance

The `halo-2-needler` branch changes the stock Needler in memory after each
map loads. The map files and profiles are not modified.

| Property | Fork rule |
| --- | --- |
| Magazine / full reload | 30 needles |
| Spread, starting / sustained | 0.5 / 1.5 degrees |
| Homing turn speed | 22.5 degrees per second |
| Maximum fire rate | 7.5 needles per second (the existing 3/s ramp-up remains) |
| Supercombine | 8 embedded needles |
| Supercombine damage and shock-wave radii | Half the map's originals |
| Projectile speed | 6 world units per second |

Reserve capacity, damage amounts, ordinary needle impacts, reload time,
models, and visual explosion effects keep the map's values. The former
2-degree inner spread cone is removed so it cannot override the tighter
outer spread. Velocity is converted to the engine's world-units-per-tick
representation; homing remains radians per second.

Solo play uses these rules. A hosted multiplayer or co-op game uses them
only when every remote participant negotiates the new optional capability.
An upstream client, an older fork, or a participant whose capability is not
known makes the whole game use its original map rules. When that participant
leaves, a matching lobby can use the rebalance again. A fork client joining
an upstream host stays stock. Protocol 29 and all upstream messages retain
their layouts; the private rules message goes only to supporting peers.

Changes during a game restore the exact saved tag values rather than applying
inverse multipliers. A magazine above the restored capacity moves excess
needles into its reserve where there is room. Weapon damage-validation caches
are invalidated on a rules change without resetting player damage history.
In-flight effects can span the short interval while a reliable policy update
reaches the other peers; the host remains authoritative.
