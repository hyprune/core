import {readFileSync} from 'node:fs';
import {validate} from '../../schema/lib/validate.mjs';
for(const entry of JSON.parse(readFileSync(process.argv[2]))) {
 if(entry.response.error)continue;
 const result=validate('ipc',entry.response,entry.method,process.argv[3] || '0.13');
 if(!result.valid)throw new Error(entry.method+': '+result.errors.join('; '));
}
console.log('PASS IPC response conformance',process.argv[3] || '0.13');
