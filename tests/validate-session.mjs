// SPDX-License-Identifier: MIT
// Method-aware offline validation of the real nested session's captured traffic.
import {readFileSync} from 'node:fs';
import {validate} from '../../schema/lib/validate.mjs';
const directory=process.argv[2] || 'dev/nested/.integration-state';
const replies=JSON.parse(readFileSync(`${directory}/transcript.json`));
const events=JSON.parse(readFileSync(`${directory}/notifications.json`));
for(const {method,response} of replies) {
 const result=validate('ipc',response,response.error?undefined:method,'0.3');
 if(!result.valid)throw new Error(`${method}: ${result.errors.join('; ')}`);
}
for(const event of events) {
 const result=validate('ipc',event,undefined,'0.3');
 if(!result.valid)throw new Error(`${event.method}: ${result.errors.join('; ')}`);
}
console.log(`Validated ${replies.length} real-core replies and ${events.length} notifications against IPC 0.3`);
