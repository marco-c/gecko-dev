/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  SELECT_REQUEST,
  WS_ADD_FRAME,
  WS_SELECT_FRAME,
  WS_OPEN_FRAME_DETAILS,
  WS_CLEAR_FRAMES,
  WS_TOGGLE_FRAME_FILTER_TYPE,
  WS_SET_REQUEST_FILTER_TEXT,
} = require("../constants");

/**
 * This structure stores list of all WebSocket frames received
 * from the backend.
 */
function WebSockets() {
  return {
    // Map with all requests (key = channelId, value = array of frame objects)
    frames: new Map(),
    frameFilterText: "",
    // Default filter type is "all",
    frameFilterType: "all",
    selectedFrame: null,
    frameDetailsOpen: false,
    currentChannelId: null,
  };
}

/**
 * When a network request is selected,
 * set the current channelId affiliated with the WebSocket connection.
 */
function setChannelId(state, action) {
  return {
    ...state,
    currentChannelId: action.httpChannelId,
    // Default filter text is empty string for a new WebSocket connection
    frameFilterText: "",
  };
}

/**
 * Appending new frame into the map.
 */
function addFrame(state, action) {
  const { httpChannelId } = action;
  const nextState = { ...state };

  const newFrame = {
    httpChannelId,
    ...action.data,
  };

  nextState.frames = mapSet(nextState.frames, newFrame.httpChannelId, newFrame);
  return nextState;
}

/**
 * Select specific frame.
 */
function selectFrame(state, action) {
  return {
    ...state,
    selectedFrame: action.frame,
    frameDetailsOpen: action.open,
  };
}

/**
 * Shows/Hides the FramePayload component.
 */
function openFrameDetails(state, action) {
  return {
    ...state,
    frameDetailsOpen: action.open,
  };
}

/**
 * Clear WS frames of the request from the state.
 */
function clearFrames(state) {
  const nextState = { ...state };
  nextState.frames = new Map(nextState.frames);
  nextState.frames.delete(nextState.currentChannelId);

  return {
    ...WebSockets(),
    // Preserving the Map objects as they might contain state for other channelIds
    frames: nextState.frames,
    // Preserving the currentChannelId as there would not be another reset of channelId
    currentChannelId: nextState.currentChannelId,
    frameFilterType: nextState.frameFilterType,
    frameFilterText: nextState.frameFilterText,
  };
}

/**
 * Toggle the frame filter type of the WebSocket connection.
 */
function toggleFrameFilterType(state, action) {
  return {
    ...state,
    frameFilterType: action.filter,
  };
}

/**
 * Set the filter text of the current channelId.
 */
function setFrameFilterText(state, action) {
  return {
    ...state,
    frameFilterText: action.text,
  };
}

/**
 * Append new item into existing map and return new map.
 */
function mapSet(map, key, value) {
  const newMap = new Map(map);
  if (newMap.has(key)) {
    const framesArray = [...newMap.get(key)];
    framesArray.push(value);
    newMap.set(key, framesArray);
    return newMap;
  }
  return newMap.set(key, [value]);
}

/**
 * This reducer is responsible for maintaining list of
 * WebSocket frames within the Network panel.
 */
function webSockets(state = WebSockets(), action) {
  switch (action.type) {
    case SELECT_REQUEST:
      return setChannelId(state, action);
    case WS_ADD_FRAME:
      return addFrame(state, action);
    case WS_SELECT_FRAME:
      return selectFrame(state, action);
    case WS_OPEN_FRAME_DETAILS:
      return openFrameDetails(state, action);
    case WS_CLEAR_FRAMES:
      return clearFrames(state);
    case WS_TOGGLE_FRAME_FILTER_TYPE:
      return toggleFrameFilterType(state, action);
    case WS_SET_REQUEST_FILTER_TEXT:
      return setFrameFilterText(state, action);
    default:
      return state;
  }
}

module.exports = {
  WebSockets,
  webSockets,
};
