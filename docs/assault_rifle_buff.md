# Fork assault rifle buff

The `assault-rifle-buff` branch reduces the assault rifle's bullet spread
angles by 25% and increases its bullet impact damage by 20%. The campaign's
2-6.5 degree spread becomes 1.5-4.875 degrees; its 10 base damage becomes 12.
Each map's original values are multiplied, so custom map values are preserved
proportionally. Bloom timing, fire rate, magazine size, aim assist, melee,
material multipliers and bullet velocity are unchanged.

Damage is host-authoritative, but shooters compute their own bullet spread.
Upstream clients cannot inherit the complete buff through the current
protocol. Both changes therefore use the Needler's optional negotiation:
solo games and lobbies whose remote peers all support the AR capability use
the buff; mixed lobbies and clients joining upstream hosts use stock rules.
An older Needler-capable peer can keep the Needler rebalance while the AR
stays stock. The capabilities are independent and do not require identical
commit hashes.

The new private policy message is sent only to supporting peers. Protocol 29
and existing message layouts remain unchanged. Transitions restore exact
original tag values and invalidate cached damage validation without resetting
player damage history. Changes are in memory; map files and profiles are
untouched. A reliable policy update can take a short interval to reach peers,
as with the Needler's fallback.
