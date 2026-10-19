import { connectAppPrompts, ingredientPrompt } from "../browser/hds-app-prompts.mjs";

export async function connectCocktailPrompts(server) {
  const prompts = await connectAppPrompts(server);
  return Object.freeze({
    get supported() { return prompts.supported; },
    send: prompts.send,
    placeGlass: () => prompts.showPrompt("Place glass; tare"),
    ingredient: (action, label, quantity) => prompts.showPrompt(ingredientPrompt(action, label, quantity)),
    serve: () => prompts.showPrompt("Mix, then serve"),
    complete: prompts.clearPrompt,
    async close() {
      await prompts.clearPrompt();
      prompts.dispose();
    },
    disconnected: prompts.dispose,
  });
}
