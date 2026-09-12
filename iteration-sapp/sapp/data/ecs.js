let actors = new Map();
let actorIdCount = 1;

let createActor = (components) => {
    components.id = actorIdCount;
    actors[actorIdCount++] = components;
};

let getActorsWithComponent = (component) => {
    console.log("getactorswithcomponent");
    return Array.from(actors).filter((item) => {
        return item[component];
    });
};

let createButton = (options) => {
    createActor({
        Rect: {
            x: 10,
            y: 10,
            width: 20,
            height: 20,
        },
        Graphics: {
            template: "Button",
        },
        Interactive: {
            mouseDown: "spawnInvader",
        },
    });
};

let engines = [];

let spriteEngine = {
    component: "Sprite",
    update: (actors) => {
        for (let actor of actors) {
            // do sprite stuff
        }
    },
};

let actionHandlers = {};

let registerActions = (actions) => {
    for (let component in actions) {
        for (let action in actions[component]) {
            if (!actionHandlers[component]) {
                actionHandlers[component] = {};
            }
            actionHandlers[component][action] = actions[component][action];
        }
    }
};

let handleComponentAction = (component, action) => {
    if (actionHandlers[component] && actionHandlers[component][action]) {
        actionHandlers[component][action]();
    }
};

let onMouseDown = (mouseEvent) => {
    let actors = getActorsWithComponent("Interaction");
    for (let actor of actors) {
        if (
            actor.Interaction.mouseDown &&
            pointInsideRect(mouseEvent.point, actor.Rect)
        ) {
            handleComponentAction("Interaction", actor.Interaction.mouseDown);
        }
    }
};

// composition of related stuff (module?)
let Invaders = {
    createInvader: () => {
        createActor({
            Rect: {
                x: 10,
                y: 10,
                width: 20,
                height: 20,
            },
            Sprite: {
                name: "invader1.png",
            },
            Collider: {
                mask: [Shot],
            },
        });
    },
    createInvaderGroup: () => {
        createActor({
            Rect: {
                x: 10,
                y: 10,
                width: 20,
                height: 20,
            },
            Velocity: {
                x: 0,
                y: 0,
            },
            Collider: {
                mask: [Shot],
            },
            // specifika components är HELT OK
            // dock ska den helst styra ETT beteende och ändå vara så generell som möjligt
            InvaderMovement: true,
        });
    },

    actions: {
        Interaction: {
            spawnInvader: () => {
                // ja vad händer när man kör denna action
            },
        },
    },
};

invader.create();

registerActions(invader.actions);

engines.push(spriteEngine);

let updateEngines = () => {
    for (let engine of engines) {
        let actors = getActorsWithComponent(engine.component);
        engine.update(actors);
    }
};
