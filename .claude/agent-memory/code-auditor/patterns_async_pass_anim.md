---
name: Async pass animation state overwrite risk
description: Server state consumption can overwrite client-side subphase during async toss animations in main.c state_recv_apply flow
type: project
---

When online async animations run client-side (online_async_toss, online_pass_anim), the main.c state consumption loop can still consume PASSING-phase state updates from the server, which overwrite pps.subphase back to CARD_PASS via the generic `pps.subphase = new_sub` assignment -- even though the client has locally advanced to TOSS_ANIM.

**Why:** The defer guards in main.c are designed for the batched (online_pass_anim) flow but have gaps for the newer async (online_async_toss) flow. The async path needs to consume confirmations and peek at PLAYING states without consuming intermediate PASSING states.

**How to apply:** When reviewing any new client-side animation mode that runs during server-driven phases, check that the subphase/phase assignment after state_recv_apply has a guard preventing overwrite of locally-advanced animation state. The pattern to watch for: `pps.subphase = (PassSubphase)view.pass_subphase` without checking if local subphase is already ahead due to animation.
