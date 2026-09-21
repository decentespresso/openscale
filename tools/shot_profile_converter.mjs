export const SCALE_PROFILE_SCHEMA = 'openscale.shot-profile/v1';
const number = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const clamp = value => Math.min(1, Math.max(0, value));
export function normalizeScaleProfile(value) {
if (!value || typeof value !== 'object') return null;
const points = value.profile;
if (!Array.isArray(points) || points.length < 2 || points.length > 128) return null;
const profile = points.map(point => [number(point?.[0], NaN), number(point?.[1], NaN)]);
if (profile.some(point => !point.every(Number.isFinite))) return null;
const ratio = number(value.ratio);
const duration = number(value.duration);
if (!value.id || !value.name || ratio <= 0 || duration < 3) return null;
return {schema:SCALE_PROFILE_SCHEMA,id:String(value.id),name:String(value.name).slice(0,80),ratio,duration,profile:profile.map(([time,flow])=>[clamp(time),Math.max(0,flow)]),...(value.source?{source:value.source}:{})};
}
function tclField(text, name) {
const match = text.match(new RegExp(`(?:^|\\n)${name}\\s+(?:\\{([^}]*)\\}|([^\\r\\n]+))`));
return match ? (match[1] ?? match[2]).trim() : '';
}
function frameGroups(text) {
const start=text.indexOf('advanced_shot'),outer=text.indexOf('{',start);if(start<0||outer<0)return[];
const groups=[];let depth=0,groupStart=-1;
for(let index=outer;index<text.length;index++){if(text[index]==='{'){depth++;if(depth===2)groupStart=index+1;}else if(text[index]==='}'){if(depth===2&&groupStart>=0)groups.push(text.slice(groupStart,index));depth--;if(depth===0)break;}}
return groups;
}
function tclDictionary(text) {
const tokens=[],pattern=/\{([^}]*)\}|([^\s]+)/g;let match;
while((match=pattern.exec(text)))tokens.push(match[1]??match[2]);
const result={};for(let index=0;index+1<tokens.length;index+=2)result[tokens[index]]=tokens[index+1];return result;
}
function de1Stages(text) {
const advanced=frameGroups(text).map(tclDictionary).map(frame=>({seconds:number(frame.seconds),flow:frame.pump==='flow'?number(frame.flow):number(frame.max_flow_or_pressure)})).filter(stage=>stage.seconds>0&&stage.flow>=0);
if(advanced.length)return advanced;
return [
{seconds:number(tclField(text,'flow_profile_preinfusion_time')),flow:number(tclField(text,'flow_profile_preinfusion'))},
{seconds:number(tclField(text,'flow_profile_hold_time')),flow:number(tclField(text,'flow_profile_hold'))},
{seconds:number(tclField(text,'flow_profile_decline_time')),flow:number(tclField(text,'flow_profile_decline'))}
].filter(stage=>stage.seconds>0);
}
export function de1PlusToScaleProfile(text, options = {}) {
const stages=de1Stages(String(text).replace(/\r/g,''));if(!stages.length)throw new Error('DE1 profile has no convertible flow stages');
const sourceDuration=stages.reduce((sum,stage)=>sum+stage.seconds,0),duration=number(options.duration,sourceDuration),maximumFlow=Math.max(...stages.map(stage=>stage.flow));
if(maximumFlow<=0)throw new Error('DE1 profile has no positive flow target or limiter');
const targetWeight=number(tclField(text,'final_desired_shot_weight_advanced'))||number(tclField(text,'final_desired_shot_weight')),dose=number(options.dose),ratio=number(options.ratio)||(dose>0&&targetWeight>0?targetWeight/dose:2);
let elapsed=0;const profile=[[0,0]];stages.forEach(stage=>{elapsed+=stage.seconds;profile.push([elapsed/sourceDuration,stage.flow/maximumFlow]);});profile[profile.length-1]=[1,0];
const name=options.name||tclField(text,'profile_title')||options.id||'Imported DE1 profile';
return normalizeScaleProfile({schema:SCALE_PROFILE_SCHEMA,id:options.id||`de1-${name.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'')}`,name,ratio,duration,profile,source:options.sourceUrl?{label:options.sourceLabel||'Imported DE1 profile',url:options.sourceUrl,note:'DE1 machine flow targets and pressure-stage flow limiters normalized for scale output.'}:undefined});
}
const tclSafe=value=>String(value).replace(/[{}\r\n]/g,' ').trim();
export function scaleProfileToDe1Plus(value, options = {}) {
const profile=normalizeScaleProfile(value);if(!profile)throw new Error('Invalid scale profile');
const maximumFlow=number(options.maximumFlow,4),dose=number(options.dose,18),frames=[];
for(let index=1;index<profile.profile.length;index++){const previous=profile.profile[index-1],current=profile.profile[index],seconds=Math.max(.01,(current[0]-previous[0])*profile.duration);frames.push(`{name {Scale ${index}} pump flow sensor coffee transition smooth flow ${(current[1]*maximumFlow).toFixed(2)} seconds ${seconds.toFixed(2)} exit_if 0}`);}
return [`advanced_shot {${frames.join(' ')}}`,'author {OpenScale Shot Flow}','beverage_type espresso',`final_desired_shot_weight_advanced ${(dose*profile.ratio).toFixed(1)}`,`profile_title {${tclSafe(profile.name)}}`,'settings_profile_type settings_2c',`profile_notes {Converted from ${SCALE_PROFILE_SCHEMA}; normalized scale output mapped to DE1 machine flow.}`].join('\n')+'\n';
}