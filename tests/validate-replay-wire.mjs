// Frozen IPC 0.12, including climbing inheritance and cursor projection.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const {validate}=await import(pathToFileURL(resolve(process.argv[2] || '../schema','lib/validate.mjs')));
const dir=process.argv[3] || 'dev/nested/.m3b-state';
const responses=JSON.parse(readFileSync(`${dir}/transcript.json`));
const events=JSON.parse(readFileSync(`${dir}/notifications.json`));
for(const {method,response} of responses) {
 const result=validate('ipc',response,response.error?undefined:method,'0.12');
 if(!result.valid) throw Error(`${method}: ${result.errors.join('; ')}`);
}
for(const event of events) {
 const result=validate('ipc',event,undefined,'0.12');
 if(!result.valid) throw Error(`${event.method}: ${result.errors.join('; ')}`);
}
console.log(`PASS IPC 0.12: ${responses.length} responses, ${events.length} notifications`);
