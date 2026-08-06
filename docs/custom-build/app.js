const featureBox=document.querySelector('#features');
const pluginBox=document.querySelector('#plugins');
const refSelect=document.querySelector('#firmware-ref');
const resolvedText=document.querySelector('#resolved');
const commandText=document.querySelector('#command');
let catalog;

function checked(container){return [...container.querySelectorAll('input:checked')].map(input=>input.value)}
function resolve(selected){const result=new Set([...catalog.defaults,...selected]);let changed=true;while(changed){changed=false;for(const feature of [...result]){for(const dependency of catalog.features[feature]||[]){if(!result.has(dependency)){result.add(dependency);changed=true}}}}return [...result].sort()}
function render(){const plugins=checked(pluginBox);const pluginData=catalog.plugins.filter(plugin=>plugins.includes(plugin.id));const requested=[...checked(featureBox),...pluginData.flatMap(plugin=>plugin.requires)];const features=resolve(requested);for(const input of featureBox.querySelectorAll('input')){input.checked=features.includes(input.value);input.disabled=features.includes(input.value)&&!checked(featureBox).includes(input.value)}for(const input of pluginBox.querySelectorAll('input')){const plugin=catalog.plugins.find(item=>item.id===input.value);input.disabled=!plugin.firmware_refs.includes(refSelect.value)||plugin.conflicts.some(item=>features.includes(item))}resolvedText.textContent=`Features: ${features.join(', ')}; plugins: ${plugins.join(', ')||'none'}`;commandText.textContent=`gh workflow run custom-build.yml \\\n  -f firmware_ref=${refSelect.value} \\\n  -f features=${features.join(',')} \\\n  -f plugins=${plugins.join(',')}`}

fetch('catalog.json').then(response=>response.json()).then(data=>{catalog=data;for(const ref of catalog.firmware_refs){refSelect.add(new Option(ref,ref))}for(const feature of Object.keys(catalog.features)){featureBox.insertAdjacentHTML('beforeend',`<label><input type="checkbox" value="${feature}"> ${feature}</label>`)}for(const plugin of catalog.plugins){pluginBox.insertAdjacentHTML('beforeend',`<label><input type="checkbox" value="${plugin.id}"> ${plugin.name} ${plugin.version}</label>`)}document.body.addEventListener('change',render);document.querySelector('#copy').addEventListener('click',()=>navigator.clipboard.writeText(commandText.textContent));render()});
