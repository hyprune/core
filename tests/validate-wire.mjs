// Optional cross-repository check against the architect's independent validator.
// Run: node tests/validate-wire.mjs /path/to/hyprune/schema
import { pathToFileURL } from 'node:url';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
const {validate}=await import(pathToFileURL(resolve(process.argv[2], 'lib/validate.mjs')));
const messages=JSON.parse(readFileSync('dev/nested/.state/transcript.json','utf8'));
for(const {method,response} of messages){
  const result=validate('ipc',response,response.error ? undefined : method);
  if(!result.valid)throw new Error(`${method}: ${result.errors.join('; ')}`);
}
console.log(`PASS: ${messages.length} live IPC responses match the independent v0 schema`);

const events=JSON.parse(readFileSync('dev/nested/.state/notifications.json','utf8'));
for(const event of events){const result=validate('ipc',event);if(!result.valid)throw new Error(result.errors.join('; '));}
console.log(`PASS: ${events.length} live notifications match the independent v0 schema`);
