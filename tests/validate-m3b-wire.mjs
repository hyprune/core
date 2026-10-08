import {readFileSync,existsSync} from 'node:fs';
import {gunzipSync} from 'node:zlib';
import {validate} from '../../schema/lib/validate.mjs';
const path=(process.argv[2] || 'dev/nested/.m3b-state').replace(/\/$/,'')+'/';
function read(name){return JSON.parse(existsSync(path+name)?readFileSync(path+name):gunzipSync(readFileSync(path+name+'.gz')));}
const responses=read('transcript.json');
for(const {method,response} of responses){const v=validate('ipc',response,response.error?undefined:method,'0.6');if(!v.valid)throw Error(method+': '+v.errors.join('; '));}
const events=read('notifications.json');
for(const event of events){const v=validate('ipc',event,undefined,'0.6');if(!v.valid)throw Error(event.method+': '+v.errors.join('; '));}
console.log(`PASS IPC 0.6: ${responses.length} responses and ${events.length} notifications`);
